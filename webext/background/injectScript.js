'use strict'

function injectScript(document) {

  function onCookieSet (cookie) {
    const xhr = new XMLHttpRequest()
    xhr.open('POST', '/__webrecorder_setcookie')
    xhr.setRequestHeader('Content-Type', 'text/plain')
    xhr.send(cookie)
  }

  function patchSetCookie () {
    Object.defineProperty(document, 'cookie', {
      get: function () {
        return __webrecorder_cookies
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
        for (const c of __webrecorder_cookies.split(';')) {
          set(c)
        }
        for (const c of decodeURIComponent(cookie).split(';')) {
          set(c)
          break
        }
        const array = []
        Object.keys(map).forEach(
          function (key, index) { array.push(key + '=' + map[key]) })
        const cookies = array.join('; ')
        if (__webrecorder_cookies !== cookies) {
          __webrecorder_cookies = cookies
          onCookieSet(cookie)
        }
      }
    })
  }

  function patchDateNow () {
    const dateNow = Date.now
    const date = Date

    // for the first second keep returning a constant time
    const startTime = dateNow() + 1000

    Date = function () { return new date(Date.now()) }
    Date.prototype = date.prototype
    Date.UTC = date.UTC
    Date.parse = date.parse
    Date.now = function () {
      return __webrecorder_response_time + Math.max(0, dateNow() - startTime)
    }
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

  patchSetCookie()
  patchDateNow()
  patchMathRandom()
}