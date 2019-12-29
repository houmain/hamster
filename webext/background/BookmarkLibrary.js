'use strict'

class BookmarkLibrary {

  constructor (backend) {
    this._backend = backend
    this._root = null
    this._recorderByTabId = { }
    this._recorderByBookmarkId = { }
    this._nextRecorderId = 1
    this._libraryBookmarkTitles = { }
    this._beforeRequestListener = (details => {
      this._handleBookmarkRequested(details.tabId, details.url)
      return { cancel: true }
    })

    browser.bookmarks.onCreated.addListener((id, info) => this._handleBookmarkCreated(id, info))
    browser.bookmarks.onChanged.addListener((id, info) => this._handleBookmarkChanged(id, info))
    browser.bookmarks.onRemoved.addListener((id, info) => this._handleBookmarkRemoved(id, info))
    browser.bookmarks.onMoved.addListener((id, info) => this._handleBookmarkMoved(id, info))
    browser.tabs.onRemoved.addListener(tabId => this.stopRecording(tabId))
  }

  set root (root) {
    this._root = root
    this._updateRequestListener()
  }

  get root () {
    return this._root
  }

  async startRecording (tabId, url, bookmark) {
    console.log('startRecording begin', tabId, bookmark.id)
    verify(tabId, url, bookmark)

    // TODO: remove sanity check
    for (const bookmarkId in this._recorderByBookmarkId) {
      if (this._recorderByBookmarkId[bookmarkId].tabId === tabId) {
        console.warn('other bookmark already recording in tab', tabId, bookmarkId)
        return
      }
    }
    const recorder = {
      recorderId: this._nextRecorderId++,
      tabId: tabId,
      url: url,
      bookmark: bookmark,
      serverUrl: null,
      onFinished: []
    }
    this._recorderByTabId[tabId] = recorder
    this._recorderByBookmarkId[bookmark.id] = recorder
    console.log('startRecording end (updated recorder)', tabId, bookmark.id, url)

    const { path, inLibrary } = await this._getBookmarkPath(bookmark.id)
    const filename = path.pop()
    return this._backend.startRecording(recorder.recorderId, path, filename,
      bookmark.url, event => this._handleRecordingOutput(tabId, recorder, event))
  }

  async stopRecording (tabId) {
    const recorder = this._recorderByTabId[tabId]
    if (!recorder) {
      return
    }
    console.log('stopRecording begin (cleared recorder)', tabId, recorder.bookmark.id)
    delete this._recorderByTabId[recorder.tabId]
    delete this._recorderByBookmarkId[recorder.bookmark.id]
    return this._backend.stopRecording(recorder.recorderId)
  }

  getOriginalUrl (url, recorder) {
    verify(url)
    recorder = recorder || this._findRecorder(url)
    if (recorder) {
      return Utils.getOrigin(recorder.url) + Utils.getPath(url)
    }
    return url
  }

  getLocalUrl (url, recorder) {
    verify(url, recorder, recorder.serverUrl)
    return Utils.getOrigin(recorder.serverUrl) + Utils.getPath(url)
  }

  _findRecorder (url) {
    verify(url)
    let origin = Utils.getOrigin(url)
    for (let tabId in this._recorderByTabId) {
      let recorder = this._recorderByTabId[tabId]
      if (recorder.serverUrl && recorder.serverUrl.startsWith(origin)) {
        return recorder
      }
    }
  }

  async _forEachLibraryBookmark (callback) {
    if (!this._root) {
      return
    }    
    const rec = function (parent) {
      for (const bookmark of parent.children) {
        if (bookmark.type === 'folder') {
          rec(bookmark)
        }
        callback(bookmark)
      }
    }
    for (const base of await browser.bookmarks.getSubTree(this._root.id)) {
      rec(base)
    }
  }

  async _getBookmarkPath (bookmarkId) {
    const bookmark = await Utils.getBookmarkById(bookmarkId)
    verify(bookmark)
    if (bookmark.id === this._root.id) {
      return { path: [], inLibrary: true }
    }
    if (!bookmark.parentId) {
      return { path: [bookmark.title], inLibrary: false }
    }
    const result = await this._getBookmarkPath(bookmark.parentId)
    result.path.push(bookmark.title)
    return result
  }

  async _findLibraryBookmark (url) {
    let result = null
    await this._forEachLibraryBookmark(function (bookmark) {
      if (bookmark.url && url.startsWith(bookmark.url)) {
        result = bookmark
      }
    })
    return result
  }

  async _callOnRecordingFinished (bookmarkId, action) {
    const recorder = this._recorderByBookmarkId[bookmarkId]
    if (!recorder) {
      return action()
    }
    return new Promise((resolve, reject) => {
      recorder.onFinished.push(async function () {
        await action()
        resolve()
      })
    })
  }

  async _moveFile (bookmarkId, sourcePath, targetPath) {
    return this._callOnRecordingFinished(bookmarkId,
      () => this._backend.moveFile(sourcePath, targetPath))
  }

  async _deleteFile (bookmarkId, path) {
    const undeleteId = bookmarkId
    return this._callOnRecordingFinished(bookmarkId,
      () => this._backend.deleteFile(path, undeleteId))
  }

  async _undeleteFile (bookmarkId) {
    const undeleteId = bookmarkId
    return this._callOnRecordingFinished(bookmarkId,
      () => this._backend.undeleteFile(undeleteId))
  }

  async _updateBookmarkUrl (id, url) {
    return browser.bookmarks.update(id, {
      url: url
    })
  }

  async _handleBookmarkCreated (id, createInfo) {
    // translate to original url when bookmarking a local url
    const url = this.getOriginalUrl(createInfo.url)
    if (url !== createInfo.url) {
      await this._updateBookmarkUrl(id, url)
    }
    await this._updateRequestListener()
    const { path, inLibrary } = await this._getBookmarkPath(id)
    if (inLibrary) {
      await this._undeleteFile(id)
    }
  }

  async _handleBookmarkChanged (id, changeInfo) {
    const bookmarkTitle = this._libraryBookmarkTitles[id]
    await this._updateRequestListener()
    const { path, inLibrary } = await this._getBookmarkPath(id)
    if (inLibrary) {
      const sourcePath = path.slice()
      sourcePath[sourcePath.length - 1] = bookmarkTitle
      return this._moveFile(id, sourcePath, path)
    }
  }

  async _handleBookmarkMoved (id, moveInfo) {
    const bookmarkTitle = this._libraryBookmarkTitles[id]
    await this._updateRequestListener()
    const source = await this._getBookmarkPath(moveInfo.oldParentId)
    const target = await this._getBookmarkPath(moveInfo.parentId)
    if (source.inLibrary) {
      source.path.push(bookmarkTitle)
      target.path.push(bookmarkTitle)
      if (target.inLibrary) {
        return this._moveFile(id, source.path, target.path)
      }
      return this._deleteFile(id, source.path)
    }
    return this._undeleteFile(id)
  }

  async _handleBookmarkRemoved (id, removeInfo) {
    const bookmarkTitle = this._libraryBookmarkTitles[id]
    await this._updateRequestListener()
    const source = await this._getBookmarkPath(removeInfo.parentId)
    source.path.push(bookmarkTitle)
    if (source.inLibrary) {
      return this._deleteFile(id, source.path)
    }
  }

  async _handleBookmarkRequested (tabId, url) {
    const bookmark = await this._findLibraryBookmark(url)
    verify(bookmark)

    // switch to tab when bookmark is already being recorded
    let recorder = this._recorderByBookmarkId[bookmark.id]
    if (recorder) {
      //console.log('switched to recording tab', recorder.tabId, bookmark.id, url)
      return browser.tabs.update(recorder.tabId, { 
        url: this.getLocalUrl(url, recorder),
        active: true
      })
    }
    // stop recording in current tab
    recorder = this._recorderByTabId[tabId]
    if (recorder) {
      await this.stopRecording(recorder.tabId)
    }
    return this._callOnRecordingFinished(bookmark.id,
      () => this.startRecording(tabId, url, bookmark))
  }

  async _handleRecordingStarted (tabId, recorder, serverUrl) {
    console.log('recording started', tabId, recorder.bookmark.id)
    recorder.serverUrl = serverUrl
    const url = serverUrl + recorder.url.substring(recorder.bookmark.url.length)
    return browser.tabs.update(tabId, { url: url })
  }

  async _handleRecordingFinished (tabId, recorder) {
    console.log('recording finished', tabId, recorder.bookmark.id)
    for (const action of recorder.onFinished) {
      await action()
    }
  }

  async _handleRecordingOutput (tabId, recorder, event) {
    if (!event) {
      await this._handleRecordingFinished(tabId, recorder)
    } else if (event.startsWith('ACCEPT ')) {
      await this._handleRecordingStarted(tabId, recorder, event.substring(7))
    } else {
      console.log(event)
    }
  }

  async _updateRequestListener () {
    const bookmarkTitles = { }
    const urlFilters = []
    await this._forEachLibraryBookmark(function (bookmark) {
      bookmarkTitles[bookmark.id] = bookmark.title
      if (bookmark.url) {
        let url = bookmark.url
        if (url.endsWith('/')) {
          url += '*'
        }
        urlFilters.push(url)
      }
    })
    this._libraryBookmarkTitles = bookmarkTitles

    window.browser.webRequest.onBeforeRequest.removeListener(
      this._beforeRequestListener)
    if (urlFilters.length > 0) {
      window.browser.webRequest.onBeforeRequest.addListener(
        this._beforeRequestListener, { urls: urlFilters }, ['blocking'])
    }
  }
}
