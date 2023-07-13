'use strict'
/* global __webrecorder */

function injectScript (document) {

  function busyWait(ms) {
    for (var begin = new Date().getTime(); new Date().getTime() < begin + ms; ) ;
  }

  function patchUrl (url) {
    if (typeof(url) !== 'string') {
      return url
    } else if (url.startsWith('.') || url.startsWith('#') || url.startsWith('?')) {
      return window.location + url
    } else if (url.startsWith('/')) {
      return window.location.origin + url
    } else {
      return url.split(__webrecorder.origin).join(window.location.origin)
    }
  }

  function patchPostMessage (window) {
    const postMessage = window.postMessage
    window.postMessage = function () {
      arguments[1] = '*'
      return postMessage.apply(this, arguments)
    }
  }

  function patchWorker (window) {
    const Worker = window.Worker
    window.Worker = function (url) {
      return new Worker(patchUrl(url))
    }
  }

  function patchWindow (window) {
    for (let i = 0; i < window.frames.length; ++i) {
      patchWindow(window.frames[i])
    }
    patchPostMessage(window)
    patchWorker(window)
  }

  function onCookieSet (cookie) {
    const xhr = new XMLHttpRequest()
    xhr.open('POST', patchUrl('/__webrecorder_setcookie'))
    xhr.setRequestHeader('Content-Type', 'text/plain')
    xhr.send(cookie)
    // wait to reduce chance of location change cancelling the request
    busyWait(50)
  }

  function patchSetCookie () {
    Object.defineProperty(document, 'cookie', {
      get: function () {
        return __webrecorder.cookies
      },
      set: function (cookie) {
        const map = { }
        const set = function (c) {
          const p = c.indexOf('=')
          const key = c.substring(0, p).trim()
          const value = c.substring(p + 1).trim()
          if (value.length > 0) {
            map[key] = value
          } else {
            delete map[key]
          }
        }
        for (const c of __webrecorder.cookies.split(';')) {
          set(c)
        }
        set(decodeURIComponent(cookie).split(';')[0])
        const array = []
        Object.keys(map).forEach(
          function (key, index) { array.push(key + '=' + map[key]) })
        const cookies = array.join('; ')
        if (__webrecorder.cookies !== cookies) {
          __webrecorder.cookies = cookies
          onCookieSet(cookie)
        }
      }
    })
  }

  function patchDateNow () {
    const Date = window.Date

    // for the first second keep returning a constant time
    const startTime = Date.now() + 1000

    const date = function (time) { return new Date(time || window.Date.now()) }
    date.prototype = Date.prototype
    date.UTC = Date.UTC
    date.parse = Date.parse
    date.now = function () {
      return __webrecorder.response_time * 1000 + Math.max(0, Date.now() - startTime)
    }
    window.Date = date
  }

  function patchMathRandom () {
    const seeds = {}
    Math.random = function () {
      const location = new Error().stack
      const hashCode =
        location.split('').reduce((a, b) => (((a << 5) - a) + b.charCodeAt(0)) | 0, 0)
      let seed = (seeds[hashCode] || 0)
      seed = (1103515245 * seed + 12345) % 0x7FFFFFFF
      seeds[hashCode] = seed
      return seed / 0x7FFFFFFF
    }
  }

  function patchHistory () {
    function patch(orig) {
      return function () {
        arguments[2] = patchUrl(arguments[2])
        return orig.apply(this, arguments)
      }
    }
    window.history.pushState = patch(window.history.pushState)
    window.history.replaceState = patch(window.history.replaceState)
  }

  patchWindow(window)
  patchHistory()
  patchSetCookie()
  patchDateNow()
  patchMathRandom()
}
