'use strict'

const NativeClient = {
  port: null,
  nextRequestId: 1,
  responseHandlers: [],
  libraryBookmarkTitles: {},
  onConnected: null,

  connect () {
    this.port = browser.runtime.connectNative(NATIVE_CLIENT_ID)
    this.port.onMessage.addListener(response => this.handleResponse(response))
    this.port.onDisconnect.addListener(port => this.handleDisconnect(port))
    if (this.onConnected) {
      this.onConnected()
    }
  },

  disconnect (error) {
    console.error('DISCONNECTED', error)
    if (this.port) {
      this.port.disconnect()
    }
    this.port = null
    this.responseHandlers = []
  },

  handleResponse (response) {
    const handler = this.responseHandlers[response.requestId]
    delete this.responseHandlers[response.requestId]

    if (response.error) {
      console.error('SEND_ERROR', response.error)
      handler.reject(response)
    }
    else {
      handler.resolve(response)
    }
  },

  handleDisconnect (port) {
    this.disconnect(port.error || browser.runtime.lastError)
  },

  send (message) {
    try {
      if (this.port) {
        this.port.postMessage(message)
        return
      }
    } catch {
    }
    this.connect()
    window.setTimeout(() => this.send(message), 10)
  },

  sendRequest (request) {
    return new Promise((resolve, reject) => {
      request.requestId = this.nextRequestId++
      this.responseHandlers[request.requestId] = { resolve, reject }
      this.send(request)
    })
  }
}
