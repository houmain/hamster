'use strict'

function verify () {
  for (var i = 0; i < arguments.length; i++) {
    if (!arguments[i]) {
      throw "verification failed"
    }
  }  
}

class Utils {
  static async getBookmarksRoot () {
    let roots = await browser.bookmarks.get('root________')
    if (browser.runtime.lastError) {
      roots = await browser.bookmarks.get('0')
    }
    return roots[0]
  }

  static async getBookmarkById (bookmarkId) {
    verify(bookmarkId)
    return (await browser.bookmarks.get(bookmarkId))[0];
  }

  static async findBookmarkByUrl (url) {
    verify(url)
    return (await browser.bookmarks.search(url))[0]
  }  

  static async findBookmarkFolderByTitle (title) {
    verify(title)
    const root = await this.getBookmarksRoot()
    const bases = await browser.bookmarks.getChildren(root.id)
    for (const base of bases) {
      const children = await browser.bookmarks.getChildren(base.id)
      const child = children.find(c => c.type === 'folder' && c.title === title)
      if (child) {
        return child
      }
    }
  }

  static getOrigin(url) {
    verify(url)
    return new URL(url).origin
  }

  static getPath(url) {
    verify(url)
    return new URL(url).pathname
  }

  static getOriginPath(url) {
    url = new URL(url)
    return url.href.substr(0, url.href.length - url.search.length)
  }

  static async getTabById (tabId) {
    verify(tabId)
    return browser.tabs.get(tabId)
  }

  static async getActiveTab() {
    return (await browser.tabs.query({
      active: true,
      currentWindow: true
    }))[0]
  }
}
