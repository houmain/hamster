'use strict'

const BookmarkLibrary = {
  recorderByTabId: { },
  recorderByBookmarkId: { },
  nextRecorderId: 1,

  async init () {
    await this.updateBeforeRequestListener()
    browser.bookmarks.onCreated.addListener((id, info) => this.handleBookmarkCreated(id, info))
    browser.bookmarks.onChanged.addListener((id, info) => this.handleBookmarkChanged(id, info))
    browser.bookmarks.onRemoved.addListener((id, info) => this.handleBookmarkRemoved(id, info))
    browser.bookmarks.onMoved.addListener((id, info) => this.handleBookmarkMoved(id, info))
    browser.tabs.onRemoved.addListener(tabId => this.stopRecording(tabId))

    browser.history.onVisited.addListener((item) => {
      if (item.url.startsWith('http://127.0.0.1')) {
        browser.history.deleteUrl({ url: item.url })
      }
    })
  },

  async getBookmarksRoot () {
    let roots = await browser.bookmarks.get('root________')
    if (browser.runtime.lastError) {
      roots = await browser.bookmarks.get('0')
    }
    return roots[0]
  },

  async findLibraryRoot () {
    const root = await this.getBookmarksRoot()
    const bases = await browser.bookmarks.getChildren(root.id)
    for (const base of bases) {
      const children = await browser.bookmarks.getChildren(base.id)
      const child = children.find(c => c.title === LIBRARY_TITLE)
      if (child) {
        return child
      }
    }
  },

  async forEachLibraryBookmark (callback) {
    const rec = function (parent) {
      for (const bookmark of parent.children) {
        if (bookmark.type === 'folder') {
          rec(bookmark)
        }
        callback(bookmark)
      }
    }
    const root = await this.findLibraryRoot()
    if (root) {
      for (const base of await browser.bookmarks.getSubTree(root.id)) {
        rec(base)
      }
    }
  },

  async getBookmarkPath (bookmarkId) {
    const bookmark = (await browser.bookmarks.get(bookmarkId))[0]
    if (bookmark.title === LIBRARY_TITLE) {
      return { path: [], inLibrary: true }
    }

    if (!bookmark.parentId) {
      return { path: [bookmark.title], inLibrary: false }
    }

    const result = await this.getBookmarkPath(bookmark.parentId)
    result.path.push(bookmark.title)
    return result
  },

  async findLibraryBookmark (url) {
    let result = null
    await this.forEachLibraryBookmark(function (bookmark) {
      if (bookmark.url && url.startsWith(bookmark.url)) {
        result = bookmark
      }
    })
    return result
  },

  async callOnRecordingFinished (bookmarkId, action) {
    const recorder = this.recorderByBookmarkId[bookmarkId]
    if (!recorder) {
      return action()
    }
    return new Promise((resolve, reject) => {
      recorder.onFinished.push(async function () {
        await action()
        resolve()
      })
    })
  },

  async moveFile (bookmarkId, sourcePath, targetPath) {
    return this.callOnRecordingFinished(bookmarkId,
      () => WebRecorder.moveFile(sourcePath, targetPath))
  },

  async deleteFile (bookmarkId, path) {
    const undeleteId = bookmarkId
    return this.callOnRecordingFinished(bookmarkId,
      () => WebRecorder.deleteFile(path, undeleteId))
  },

  async undeleteFile (bookmarkId) {
    const undeleteId = bookmarkId
    return this.callOnRecordingFinished(bookmarkId,
      () => WebRecorder.undeleteFile(undeleteId))
  },

  async handleBookmarkCreated (id, createInfo) {
    await this.updateBeforeRequestListener()
    const { path, inLibrary } = await this.getBookmarkPath(id)
    if (inLibrary) {
      await this.undeleteFile(id)
    }

    // automatically start recording
    for (const currentTab of await browser.tabs.query({ active: true })) {
      if (currentTab.url === createInfo.url) {
        return browser.tabs.reload(currentTab.id)
      }
    }
  },

  async handleBookmarkChanged (id, changeInfo) {
    const bookmarkTitle = this.libraryBookmarkTitles[id]
    await this.updateBeforeRequestListener()
    const { path, inLibrary } = await this.getBookmarkPath(id)
    if (inLibrary) {
      const sourcePath = path.slice()
      sourcePath[sourcePath.length - 1] = bookmarkTitle
      return this.moveFile(id, sourcePath, path)
    }
  },

  async handleBookmarkMoved (id, moveInfo) {
    const bookmarkTitle = this.libraryBookmarkTitles[id]
    await this.updateBeforeRequestListener()
    const source = await this.getBookmarkPath(moveInfo.oldParentId)
    const target = await this.getBookmarkPath(moveInfo.parentId)
    if (source.inLibrary) {
      source.path.push(bookmarkTitle)
      target.path.push(bookmarkTitle)
      if (target.inLibrary) {
        return this.moveFile(id, source.path, target.path)
      }
      return this.deleteFile(id, source.path)
    }
    return this.undeleteFile(id)
  },

  async handleBookmarkRemoved (id, removeInfo) {
    const bookmarkTitle = this.libraryBookmarkTitles[id]
    await this.updateBeforeRequestListener()
    const source = await this.getBookmarkPath(removeInfo.parentId)
    source.path.push(bookmarkTitle)
    if (source.inLibrary) {
      return this.deleteFile(id, source.path)
    }
  },

  async handleBookmarkRequested (tabId, url) {
    const bookmark = await this.findLibraryBookmark(url)

    // switch to tab when bookmark is already being recorded
    if (bookmark) {
      const recorder = this.recorderByBookmarkId[bookmark.id]
      if (recorder) {
        console.log('switched to recording tab', recorder.tabId, bookmark.id, url)
        // url = recorder.serverUrl + url.substring(recorder.url.length)
        return browser.tabs.update(recorder.tabId, { active: true })
      }
    }

    // stop recording in current tab
    const recorder = this.recorderByTabId[tabId]
    if (recorder) {
      await this.stopRecording(recorder.tabId)
    }
    return this.callOnRecordingFinished(bookmark.id,
      () => this.startRecording(tabId, url, bookmark))
  },

  async startRecording (tabId, url, bookmark) {
    console.log('startRecording begin', tabId, bookmark.id)

    // TODO: remove sanity check
    for (const bookmarkId in this.recorderByBookmarkId) {
      if (this.recorderByBookmarkId[bookmarkId].tabId === tabId) {
        console.warn('other bookmark already recording in tab', tabId, bookmarkId)
        return
      }
    }

    const recorder = {
      recorderId: this.nextRecorderId++,
      tabId: tabId,
      url: url,
      bookmark: bookmark,
      serverUrl: null,
      onFinished: []
    }
    this.recorderByTabId[tabId] = recorder
    this.recorderByBookmarkId[bookmark.id] = recorder
    console.log('startRecording end (updated recorder)', tabId, bookmark.id, url)

    const { path, inLibrary } = await this.getBookmarkPath(bookmark.id)
    const filename = path.pop()
    return WebRecorder.startRecording(recorder.recorderId, path, filename,
      bookmark.url, event => this.handleRecordingOutput(tabId, recorder, event))
  },

  async handleRecordingStarted (tabId, recorder, serverUrl) {
    console.log('recording started', tabId, recorder.bookmark.id)
    recorder.serverUrl = serverUrl
    const url = serverUrl + recorder.url.substring(recorder.bookmark.url.length)
    return browser.tabs.update(tabId, { url: url })
  },

  async handleRecordingFinished (tabId, recorder) {
    console.log('recording finished', tabId, recorder.bookmark.id)
    for (const action of recorder.onFinished) {
      await action()
    }
  },

  async handleRecordingOutput (tabId, recorder, event) {
    if (!event) {
      await this.handleRecordingFinished(tabId, recorder)
    } else if (event.startsWith('ACCEPT ')) {
      await this.handleRecordingStarted(tabId, recorder, event.substring(7))
    } else {
      console.log(event)
    }
  },

  async stopRecording (tabId) {
    const recorder = this.recorderByTabId[tabId]
    if (!recorder) {
      return
    }
    console.log('stopRecording begin (cleared recorder)', tabId, recorder.bookmark.id)
    delete this.recorderByTabId[recorder.tabId]
    delete this.recorderByBookmarkId[recorder.bookmark.id]
    return WebRecorder.stopRecording(recorder.recorderId)
  },

  handleBeforeRequest (details) {
    BookmarkLibrary.handleBookmarkRequested(details.tabId, details.url)
    return { cancel: true }
  },

  async updateBeforeRequestListener () {
    const bookmarkTitles = { }
    const urlFilters = []
    await this.forEachLibraryBookmark(function (bookmark) {
      bookmarkTitles[bookmark.id] = bookmark.title
      if (bookmark.url) {
        let url = bookmark.url
        if (url.endsWith('/')) {
          url += '*'
        }
        urlFilters.push(url)
      }
    })
    this.libraryBookmarkTitles = bookmarkTitles

    window.browser.webRequest.onBeforeRequest.removeListener(
      this.handleBeforeRequest)
    if (urlFilters.length > 0) {
      window.browser.webRequest.onBeforeRequest.addListener(
        this.handleBeforeRequest, { urls: urlFilters }, ['blocking'])
    }
  }
}
