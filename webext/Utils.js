'use strict'

const debugEnabled = false
const debugLongAsyncDurationMs = 100

function DEBUG () {
  if (debugEnabled) {
    console.log('DEBUG: ', ...arguments)
  }
}

function DEBUG_LOG_ASYNC_CALLS (object) {
  if (debugEnabled) {
    for (const name of Object.getOwnPropertyNames(object)) {
      const call = object[name]
      if (typeof call === 'function' &&
          call.constructor.name === 'AsyncFunction') {
        object[name] = async function () {
          const begin = new Date().getTime()
          const result = await call.apply(this, arguments)
          const end = new Date().getTime()
          if (end - begin > debugLongAsyncDurationMs) {
            console.warn(`calling ${name} took ${end - begin}ms`)
            console.trace()
          }
          return result
        }
      }
    }
  }
}

function verify () {
  for (let i = 0; i < arguments.length; i++) {
    if (!arguments[i]) {
      console.error('verification failed')
      console.trace()
      throw 'verification failed'
    }
  }
}

class Utils {
  static isHttpUrl (url) {
    return (url &&
      (url.startsWith('http://') ||
       url.startsWith('https://')))
  }

  static isLocalUrl (url) {
    return (url &&
      url.match(/^https?:\/\/(127\.0\.0\.1|localhost)[:\/]/))
  }

  static patchUrl (url, originalUrl, localUrl) {
    if (!Utils.isHttpUrl(url) || 
        Utils.isLocalUrl(url)) {
      return url
    }
    verify(originalUrl)
    verify(localUrl)

    // replace [http://]127.0.0.1[:port] in the middle
    if (url.indexOf(encodeURIComponent(localUrl.hostname) >= 0)) {
      url = url.split(encodeURIComponent(localUrl.origin)).join(
        encodeURIComponent(originalUrl.origin))
      url = url.split(encodeURIComponent(localUrl.host)).join(
        encodeURIComponent(originalUrl.host))
      url = url.split(encodeURIComponent(localUrl.hostname)).join(
        encodeURIComponent(originalUrl.hostname))
    }

    // convert to local url
    if (Utils.getOrigin(url) === originalUrl.origin) {
      return localUrl.origin + Utils.getPathQuery(url)
    }
    return localUrl.origin + '/' + url
  }

  static sleep (ms) {
    return new Promise(resolve => setTimeout(resolve, ms))
  }

  static async getBookmarkBaseFolders () {
    for (const bookmarkId of [
      'root________', // Firefox
      '0']) { // Chromium
      try {
        return await browser.bookmarks.getChildren(bookmarkId)
      } catch {
      }
    }
  }

  static async getBookmarkById (bookmarkId) {
    verify(bookmarkId)
    return (await browser.bookmarks.get(bookmarkId))[0]
  }

  static getOrigin (url) {
    verify(this.isHttpUrl(url))
    return new URL(url).origin
  }

  static getPathQuery (url) {
    verify(this.isHttpUrl(url))
    url = new URL(url)
    return url.href.substring(url.origin.length)
  }

  static getHostPathWithoutWWW (url) {
    verify(this.isHttpUrl(url))
    url = new URL(url)
    return (url.host.startsWith('www.')
      ? url.host.substring(4)
      : url.host) + url.pathname
  }

  static getHostnamePathWithoutWWW (url) {
    verify(this.isHttpUrl(url))
    url = new URL(url)
    return (url.hostname.startsWith('www.')
      ? url.hostname.substring(4)
      : url.hostname) + url.pathname
  }

  static getUrlMatchPattern (url, urlFilters) {
    verify(this.isHttpUrl(url))
    const hostnamePath = this.getHostnamePathWithoutWWW(url)
    urlFilters.push('*://www.' + hostnamePath + '*')
    urlFilters.push('*://' + hostnamePath + '*')
  }

  static async getTabById (tabId) {
    verify(tabId)
    return browser.tabs.get(tabId)
  }

  static async getActiveTab () {
    return (await browser.tabs.query({
      active: true,
      currentWindow: true
    }))[0]
  }

  static async findTabsMatchingUrl (url) {
    verify(url)
    const urlFilters = []
    this.getUrlMatchPattern(url, urlFilters)
    const tabs = await browser.tabs.query({ url: urlFilters })
    // also filter by port, which is ignored in query
    const origin = this.getOrigin(url)
    if (this.isLocalUrl(origin)) {
      return tabs.filter(tab => { return tab.url.startsWith(origin) })
    }
    return tabs
  }

  static async tryReloadTab (tabId) {
    try {
      DEBUG('reloading tab', tabId, (await browser.tabs.get(tabId)).url)
      return browser.tabs.reload(tabId)
    } catch {
      // tab already closed
    }
  }

  static async tryUpdateBookmarkUrl (bookmarkId, url) {
    try {
      DEBUG('updating bookmark url', (await Utils.getBookmarkById(bookmarkId)).url, 'to', url)
      return browser.bookmarks.update(bookmarkId, { url: url })
    } catch {
      // bookmark deleted
    }
  }

  static async setSetting (key, value) {
    const settings = { }
    settings[key] = value
    return browser.storage.local.set(settings)
  }

  static async setDefaultSetting (key, defaultValue) {
    const settings = await browser.storage.local.get(key)
    if (!settings || typeof settings[key] === 'undefined') {
      return this.setSetting(key, defaultValue)
    }
  }

  static async getSetting (key, defaultValue) {
    const settings = await browser.storage.local.get(key)
    if (!settings || typeof settings[key] === 'undefined') {
      return defaultValue
    }
    return settings[key]
  }

  static updateSelectOptions (id, options) {
    const select = document.getElementById(id)
    select.textContent = ''
    for (const opt of options) {
      const option = document.createElement('option')
      option.setAttribute('value', opt.value)
      option.setAttribute('title', opt.title)
      for (const key in (opt.data || [])) {
        option.setAttribute('data-' + key, opt.data[key])
      }
      if (opt.selected) {
        option.setAttribute('selected', true)
      }
      option.appendChild(document.createTextNode(opt.title))
      select.appendChild(option)
    }
  }

  static localize (id, attribute, message) {
    document.getElementById(id)[attribute] = browser.i18n.getMessage(message)
  }

  static getReadableFileSize (bytes) {
    const units = ['bytes', 'KiB', 'MiB', 'GiB']
    for (let u = 0, count = bytes * 1.0; ; ++u, count /= 1024) {
      if (count < 1024 || u === units.length - 1) {
        return count.toFixed(u === 0 ? 0 : 1) + ' ' + units[u]
      }
    }
  }
}

DEBUG_LOG_ASYNC_CALLS(Utils)
