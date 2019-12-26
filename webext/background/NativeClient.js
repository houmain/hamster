'use strict'

class NativeClient {
  constructor() {
    this._port = null,
    this._nextRequestId = 1
    this._responseHandlers = []
    this._libraryBookmarkTitles = {}
    this._onConnected = null
  }

  set onConnected (callback)  { 
    this._onConnected = callback 
  }

  sendRequest (request) {
    return new Promise((resolve, reject) => {
      request.requestId = this._nextRequestId++
      this._responseHandlers[request.requestId] = { resolve, reject }
      this._send(request)
    })
  }
  
  _connect () {
    this._port = browser.runtime.connectNative(NATIVE_CLIENT_ID)
    this._port.onMessage.addListener(response => this._handleResponse(response))
    this._port.onDisconnect.addListener(_port => this._handleDisconnect(_port))
    if (this._onConnected) {
      this._onConnected()
    }
  }

  _disconnect (error) {
    console.error('DISCONNECTED', error)
    if (this._port) {
      this._port.disconnect()
    }
    this._port = null
    this._responseHandlers = []
  }

  _handleResponse (response) {
    const handler = this._responseHandlers[response.requestId]
    delete this._responseHandlers[response.requestId]
    if (response.error) {
      console.error('SEND_ERROR', response.error)
      handler.reject(response)
    } else {
      handler.resolve(response)
    }
  }

  _handleDisconnect (_port) {
    this._disconnect(_port.error || browser.runtime.lastError)
  }

  _send (message) {
    let sent = false
    try {
      if (this._port) {
        this._port.postMessage(message)
        sent = true
      }
    } finally {
      if (!sent) {
        this._connect()
        window.setTimeout(() => this._send(message), 10)
      }
    }
  }
}
