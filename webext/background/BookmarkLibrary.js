'use strict'

class BookmarkLibrary {

  constructor (backend) {
    this._backend = backend
    this._rootId = null
    this._recorderByBookmarkId = { }
    this._recorderByTabId = { }
    this._recentRecorders = []
    this._nextRecorderId = 1
    this._libraryBookmarkTitles = { }

    browser.bookmarks.onCreated.addListener((id, info) => this._handleBookmarkCreated(id, info))
    browser.bookmarks.onChanged.addListener((id, info) => this._handleBookmarkChanged(id, info))
    browser.bookmarks.onRemoved.addListener((id, info) => this._handleBookmarkRemoved(id, info))
    browser.bookmarks.onMoved.addListener((id, info) => this._handleBookmarkMoved(id, info))
    browser.tabs.onRemoved.addListener((id) => this._stopRecordingInTab(id))
    browser.webRequest.onBeforeRequest.addListener(
      async (details) => await this._handleBeforeRequest(details),
      { urls: [ 'http://*/*', 'https://*/*' ] }, [ 'blocking' ])


    backend.injectScript(`(${injectScript})(document)`)
  }

  async setRootId (rootId) {
    verify(rootId)
    this._rootId = rootId
    await this._restoreRecentRecorders()
    return this._updateLibraryBookmarkList()
  }

  get rootId () {
    return this._rootId
  }

  getOriginalUrl (url) {
    verify(url)
    if (Utils.isLocalUrl(url)) {
      const recorder = this._findRecentRecorder(url)
      if (recorder) {
        return Utils.getOrigin(recorder.bookmarkUrl) + Utils.getPathQuery(url)
      }
    }
    return url
  }

  async getBookmarkPath (bookmarkId) {
    const bookmark = await Utils.getBookmarkById(bookmarkId)
    if (bookmark.id === this._rootId) {
      return { path: [], inLibrary: true }
    }
    if (!bookmark.parentId) {
      return { path: [bookmark.title], inLibrary: false }
    }
    const result = await this.getBookmarkPath(bookmark.parentId)
    result.path.push(bookmark.title)
    return result
  }

  addRecordingEventHandler (bookmarkId, handler) {
    const recorder = this._recorderByBookmarkId[bookmarkId]
    if (recorder) {
      for (let event of recorder.events) {
        handler(event)
      }
      recorder.onEvent.push(handler)
    }
  }

  async _getBookmarks () {
    if (!this._rootId) {
      return []
    }
    let bookmarks = []
    const rec = function (bookmark) {
      for (const child of bookmark.children) {
        if (child.type === 'folder') {
          rec(child)
        }
        bookmarks.push(child)
      }
    }
    for (const base of await browser.bookmarks.getSubTree(this._rootId)) {
      rec(base)
    }
    return bookmarks
  }

  async _tryGetBookmarkPath (bookmarkId) {
    try {
      return await this.getBookmarkPath(bookmarkId)
    } catch {
      // bookmark removed
      return { path: [], inLibrary: false }
    }
  }

  async _updateLibraryBookmarkList () {
    const bookmarkTitles = { }
    for (const bookmark of await this._getBookmarks()) {
      bookmarkTitles[bookmark.id] = bookmark.title
    }
    this._libraryBookmarkTitles = bookmarkTitles
  }

  async _startRecording (bookmarkId, url, initialTabId) {
    verify(bookmarkId, url, initialTabId)
    verify(!Utils.isLocalUrl(url))
    verify(!this._recorderByBookmarkId[bookmarkId])
    verify(!this._recorderByTabId[initialTabId])

    //DEBUG('start recording', url)
    const bookmark = await Utils.getBookmarkById(bookmarkId)
    const recorder = {
      recorderId: this._nextRecorderId++,
      url: url,
      bookmarkId: bookmark.id,
      bookmarkUrl: bookmark.url,
      localOrigin: null,
      tabIds: [ initialTabId ],
      finishing: false,
      events: [],
      onFinished: [],
      onEvent: []
    }
    this._recorderByBookmarkId[bookmark.id] = recorder
    this._recorderByTabId[initialTabId] = recorder

    const { path, inLibrary } = await this.getBookmarkPath(bookmark.id)
    await this._backend.startRecording(
      recorder.recorderId, path, bookmark.url,
      event => this._handleRecordingOutput(recorder, event))

    return recorder
  }

  async _startRecordingInTab (bookmarkId, url, tabId) {
    verify(!this._recorderByTabId[tabId])

    const recorder = this._recorderByBookmarkId[bookmarkId]
    if (!recorder) {
      return this._startRecording(bookmarkId, url, tabId)
    }

    verify(recorder.tabIds.indexOf(tabId) === -1)
    while (recorder.finishing) {
      await Utils.sleep(10)
    }

    this._recorderByTabId[tabId] = recorder
    recorder.tabIds.push(tabId)
    return recorder
  }

  async _handleRecordingOutput (recorder, event) {
    if (!event) {
      await this._handleRecordingFinished(recorder)
    } else if (event.startsWith('ACCEPT ')) {
      await this._handleRecordingStarted(recorder, event.substring(7))
    } else {
      recorder.events.push(event)
      for (let i = 0; i < recorder.onEvent.length; ) {
        try {
          const handler = recorder.onEvent[i]
          await handler(event)
          ++i
        }
        catch {
          recorder.onEvent.splice(i, 1)
        }
      }
    }
  }

  async _handleRecordingStarted (recorder, localUrl) {
    //DEBUG('recording started', recorder.url, 'at', localUrl)
    await this._updateRecentRecorders(localUrl, recorder.bookmarkUrl)
    await Utils.tryUpdateBookmarkUrl(recorder.bookmarkId, localUrl)
    await this._updateLibraryBookmarkList()
    await this._reloadTabs(recorder.bookmarkId, recorder.tabIds[0])
    recorder.localOrigin = Utils.getOrigin(localUrl)
  }

  async _restoreRecentRecorders () {
    this._recentRecorders = await Utils.getSetting('recent-recorders', [])

    // undo patching bookmark url (just in case browser crashed)
    for (const recorder of this._recentRecorders) {
      const bookmark = await this._findBookmarkByUrl(recorder.localUrl)
      if (bookmark && bookmark.url.startsWith(recorder.localUrl)) {
        Utils.tryUpdateBookmarkUrl(bookmark.id, recorder.bookmarkUrl)
      }
    }
  }

  async _updateRecentRecorders (localUrl, bookmarkUrl) {
    //DEBUG('updating recent recorders', localUrl, bookmarkUrl)
    this._recentRecorders.unshift({ localUrl: localUrl, bookmarkUrl: bookmarkUrl })
    while (this._recentRecorders.length > 20) {
      this._recentRecorders.pop()
    }
    return Utils.setSetting('recent-recorders', this._recentRecorders)
  }

  async _stopRecordingInTab (tabId) {
    const recorder = this._recorderByTabId[tabId]
    if (recorder) {
      delete this._recorderByTabId[tabId]

      recorder.tabIds.splice(recorder.tabIds.indexOf(tabId), 1)
      if (recorder.tabIds.length === 0) {
        this._stopRecording(recorder.bookmarkId)
      }
    }
  }

  async _stopRecording (bookmarkId) {
    const recorder = this._recorderByBookmarkId[bookmarkId]
    if (recorder && !recorder.finishing) {
      //DEBUG('stop recording', recorder.url)
      recorder.finishing = true

      await Utils.tryUpdateBookmarkUrl(bookmarkId, recorder.bookmarkUrl)
      await this._updateLibraryBookmarkList()
      while (recorder.tabIds.length > 0) {
        const tabId = recorder.tabIds[0]
        await this._stopRecordingInTab(tabId)
        await Utils.tryReloadTab(tabId)
      }
      return this._backend.stopRecording(recorder.recorderId)
    }
  }

  async _handleRecordingFinished (recorder) {
    //DEBUG('recording finished', recorder.url)
    delete this._recorderByBookmarkId[recorder.bookmarkId]
    for (const action of recorder.onFinished) {
      await action()
    }
    recorder.finishing = false

    // automatically update index
    const { path, inLibrary } = await this._tryGetBookmarkPath(recorder.bookmarkId)
    if (inLibrary) {
      this._backend.updateSearchIndex(path)
    }
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

  _findRecentRecorder (localUrl) {
    verify(Utils.isLocalUrl(localUrl))
    for (const recorder of this._recentRecorders) {
      if (localUrl.startsWith(recorder.localUrl)) {
        return recorder
      }
    }
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

  async _findTabsByBookmarkUrl (bookmarkId) {
    const recorder = this._recorderByBookmarkId[bookmarkId]
    const bookmark = await Utils.getBookmarkById(bookmarkId)
    const url = (recorder ? recorder.bookmarkUrl : bookmark.url)
    return Utils.findTabsMatchingUrl(url)
  }

  async _reloadTabs (bookmarkId, excludeTabId) {
    for (let tab of await this._findTabsByBookmarkUrl(bookmarkId)) {
      if (tab.id !== excludeTabId) {
        Utils.tryReloadTab(tab.id)
      }
    }
  }

  async _handleBookmarkCreated (bookmarkId, createInfo) {
    if (createInfo.url) {
      // translate to original url when bookmarking a local url
      const url = this.getOriginalUrl(createInfo.url)
      if (url !== createInfo.url) {
        await Utils.tryUpdateBookmarkUrl(bookmarkId, url)
      }
    }
    await this._updateLibraryBookmarkList()
    const { path, inLibrary } = await this.getBookmarkPath(bookmarkId)
    if (inLibrary) {
      await this._undeleteFile(bookmarkId)
    }
    return this._reloadTabs(bookmarkId)
  }

  async _handleBookmarkChanged (bookmarkId, changeInfo) {
    const bookmarkTitle = this._libraryBookmarkTitles[bookmarkId]
    await this._updateLibraryBookmarkList()
    if (changeInfo.title) {
      const { path, inLibrary } = await this.getBookmarkPath(bookmarkId)
      if (inLibrary) {
        const sourcePath = path.slice()
        sourcePath[sourcePath.length - 1] = bookmarkTitle
        await this._moveFile(bookmarkId, sourcePath, path)
      }
    }
  }

  async _handleBookmarkMoved (bookmarkId, moveInfo) {
    const bookmarkTitle = this._libraryBookmarkTitles[bookmarkId]
    await this._updateLibraryBookmarkList()
    const source = await this.getBookmarkPath(moveInfo.oldParentId)
    const target = await this.getBookmarkPath(moveInfo.parentId)
    if (source.inLibrary) {
      source.path.push(bookmarkTitle)
      target.path.push(bookmarkTitle)
      if (target.inLibrary) {
        return this._moveFile(bookmarkId, source.path, target.path)
      }
      await this._stopRecording(bookmarkId)
      return this._deleteFile(bookmarkId, source.path)
    }
    await this._reloadTabs(bookmarkId)
    return this._undeleteFile(bookmarkId)
  }

  async _handleBookmarkRemoved (bookmarkId, removeInfo) {
    const bookmarkTitle = this._libraryBookmarkTitles[bookmarkId]
    await this._updateLibraryBookmarkList()
    await this._stopRecording(bookmarkId)
    const source = await this.getBookmarkPath(removeInfo.parentId)
    source.path.push(bookmarkTitle)
    if (source.inLibrary) {
      await this._deleteFile(bookmarkId, source.path)
    }
  }

  _patchUrl (url, recorder) {
    verify(!Utils.isLocalUrl(url), Utils.isHttpUrl(url))
    verify(recorder.localOrigin)
    // try to convert to relative
    if (Utils.getOrigin(url) == Utils.getOrigin(recorder.bookmarkUrl)) {
      return recorder.localOrigin + Utils.getPathQuery(url)
    }
    // patch to absolute
    return recorder.localOrigin + '/' + url
  }

  async _handleBeforeRequest (details) {
    let { url, tabId, type } = details
    if (tabId < 0) {
      return
    }

    let recorder = this._recorderByTabId[tabId]
    if (type === 'main_frame') {
      const original = this.getOriginalUrl(url)
      const bookmark = await this._findBookmarkByUrl(original)

      if (Utils.isLocalUrl(url) && !bookmark && original !== url) {
        // restore original url of local url not belonging to a bookmark
        return { redirectUrl: original }
      }

      if (recorder) {
        // stop recording when navigating away
        if (!bookmark || this._recorderByBookmarkId[bookmark.id] !== recorder) {
          await this._stopRecordingInTab(tabId)
          recorder = null
        }
      }

      if (bookmark && !recorder) {
        // start recording
        recorder = await this._startRecordingInTab(bookmark.id, original, tabId)
        url = original
      }
    }

    if (!Utils.isLocalUrl(url) && recorder) {
      // redirect resources to recorder
      if (recorder.finishing) {
        return { cancel: true }
      }
      while (!recorder.localOrigin) {
        await Utils.sleep(10)
      }
      const patchedUrl = this._patchUrl(url, recorder)
      //DEBUG('redirecting request to', patchedUrl)
      return { redirectUrl: patchedUrl }
    }
  }

  async _findBookmarkByUrl (url) {
    url = Utils.getHostPathWithoutWWW(url)
    for (const bookmarkId in this._recorderByBookmarkId) {
      const recorder = this._recorderByBookmarkId[bookmarkId]
      if (!recorder.finishing &&
          url.startsWith(Utils.getHostPathWithoutWWW(recorder.bookmarkUrl))) {
        return Utils.getBookmarkById(bookmarkId)
      }
    }
    for (const bookmark of await this._getBookmarks()) {
      if (bookmark.url &&
          url.startsWith(Utils.getHostPathWithoutWWW(bookmark.url))) {
        return bookmark
      }
    }
  }
}
