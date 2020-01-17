'use strict'

class NativeClient {
  constructor (nativeClientId) {
    this._nativeClientId = nativeClientId
    this._port = null
    this._nextRequestId = 1
    this._responseHandlers = []
    this._libraryBookmarkTitles = {}
    this._connectionHandlers = []
  }

  addConnectionHandler (callback)  {
    this._connectionHandlers.push(callback)
  }

  sendRequest (request) {
    return new Promise((resolve, reject) => {
      request.requestId = this._nextRequestId++
      this._responseHandlers[request.requestId] = { resolve, reject }
      this._send(request)
    })
  }

  _connect () {
    this._port = browser.runtime.connectNative(this._nativeClientId)
    this._port.onMessage.addListener(response => this._handleResponse(response))
    this._port.onDisconnect.addListener(_port => this._handleDisconnect(_port))
    for (const callback of this._connectionHandlers) {
      callback()
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
        this._port.postMessage(message)
      }
    }
  }
}
