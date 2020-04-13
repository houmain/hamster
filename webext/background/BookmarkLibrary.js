'use strict'

class BookmarkLibrary {

  constructor (backend) {
    this._backend = backend
    this._rootId = null
    this._recorderByBookmarkId = { }
    this._recentRecorders = []
    this._nextRecorderId = 1
    this._libraryBookmarkTitles = { }
    this._beforeRequestListener = (details) => this._handleBookmarkRequested(details)

    browser.bookmarks.onCreated.addListener((id, info) => this._handleBookmarkCreated(id, info))
    browser.bookmarks.onChanged.addListener((id, info) => this._handleBookmarkChanged(id, info))
    browser.bookmarks.onRemoved.addListener((id, info) => this._handleBookmarkRemoved(id, info))
    browser.bookmarks.onMoved.addListener((id, info) => this._handleBookmarkMoved(id, info))
    browser.tabs.onUpdated.addListener((id, info) => this._handleTabUpdated(id, info))
    browser.tabs.onRemoved.addListener((id) => this._stopRecordersWithoutTabs(id))
  }

  async setRootId (rootId) {
    verify(rootId)
    this._rootId = rootId
    await this._restoreRecentRecorders()
    return this._updateBeforeRequestListener()
  }

  get rootId () {
    return this._rootId
  }

  getOriginalUrl (url) {
    const recorder = this._findRecentRecorder(url)
    if (recorder) {
      return Utils.getOrigin(recorder.bookmarkUrl) + Utils.getPathQuery(url)
    }
    return url
  }

  _getLocalUrl (url, recorder) {
    verify(url, recorder, recorder.localUrl)
    return Utils.getOrigin(recorder.localUrl) + Utils.getPathQuery(url)
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

  async _getBookmarkPath (bookmarkId) {
    const bookmark = await Utils.getBookmarkById(bookmarkId)
    if (bookmark.id === this._rootId) {
      return { path: [], inLibrary: true }
    }
    if (!bookmark.parentId) {
      return { path: [bookmark.title], inLibrary: false }
    }
    const result = await this._getBookmarkPath(bookmark.parentId)
    result.path.push(bookmark.title)
    return result
  }

  async _tryGetBookmarkPath (bookmarkId) {
    try {
      return await this._getBookmarkPath(bookmarkId)
    } catch {
      // bookmark removed
      return { path: [], inLibrary: false }
    }
  }

  async _handleTabUpdated (tabId, change) {
    if (change.url) {
      // restore original URL on undo close tab
      const original = this.getOriginalUrl(change.url)
      if (original !== change.url) {
        const recorder = this._findRecorder(change.url)
        if (!recorder) {
          return Utils.tryUpdateTabUrl(tabId, original)
        }
      }
    }
  }

  async _startRecording (bookmarkId, url, tabId) {
    verify(bookmarkId, url)
    verify(!url.startsWith('http://127.0.0.1'))

    let recorder = this._recorderByBookmarkId[bookmarkId]
    if (recorder) {
      if (recorder.finishing) {
        //DEBUG('waiting for recording to finish', recorder.url)
        return this._callOnRecordingFinished(bookmarkId,
          () => this._startRecording (bookmarkId, url, tabId))
      }
      return
    }

    await this._stopRecordersWithoutTabs(tabId)

    //DEBUG('start recording', url)
    const bookmark = await Utils.getBookmarkById(bookmarkId)
    recorder = {
      recorderId: this._nextRecorderId++,
      url: url,
      bookmarkId: bookmark.id,
      bookmarkUrl: bookmark.url,
      localUrl: null,
      initialTabId: tabId,
      finishing: false,
      onFinished: []
    }
    this._recorderByBookmarkId[bookmark.id] = recorder

    const { path, inLibrary } = await this._getBookmarkPath(bookmark.id)
    await this._backend.startRecording(
      recorder.recorderId, path, bookmark.url,
      event => this._handleRecordingOutput(recorder, event))
  }

  async _handleRecordingOutput (recorder, event) {
    if (!event) {
      await this._handleRecordingFinished(recorder)
    } else if (event.startsWith('ACCEPT ')) {
      await this._handleRecordingStarted(recorder, event.substring(7))
    } else {
      //console.log(event)
    }
  }

  async _handleRecordingStarted (recorder, localUrl) {
    recorder.localUrl = localUrl
    //DEBUG('recording started', recorder.url, 'at', recorder.localUrl)
    await this._updateRecentRecorders(recorder.localUrl, recorder.bookmarkUrl)
    await Utils.tryUpdateBookmarkUrl(recorder.bookmarkId, recorder.localUrl)
    await this._updateBeforeRequestListener()
    if (recorder.initialTabId) {
      await Utils.tryUpdateTabUrl(recorder.initialTabId, localUrl)
    }
    await this._reloadTabs(recorder.bookmarkId)
  }

  async _restoreRecentRecorders () {
    this._recentRecorders = await Utils.getSetting('recent-recorders', [])

    // undo patching bookmark url (just in case browser crashed)
    //for (const recorder of this._recentRecorders) {
    //  const bookmark = await this._findBookmarkByUrl(recorder.localUrl)
    //  if (bookmark && bookmark.url.startsWith(recorder.localUrl)) {
    //    Utils.tryUpdateBookmarkUrl(bookmark.id, recorder.bookmarkUrl)
    //  }
    //}
  }

  async _updateRecentRecorders (localUrl, bookmarkUrl) {
    //DEBUG('updating recent recorders', localUrl, bookmarkUrl)
    this._recentRecorders.unshift({ localUrl: localUrl, bookmarkUrl: bookmarkUrl })
    while (this._recentRecorders.length > 20) {
      this._recentRecorders.pop()
    }
    return Utils.setSetting('recent-recorders', this._recentRecorders)
  }


  async _stopRecording (bookmarkId, closingTabId) {
    const recorder = this._recorderByBookmarkId[bookmarkId]
    if (!recorder) {
      return
    }
    if (!recorder.finishing) {
      //DEBUG('stop recording', recorder.url)
      recorder.finishing = true
      await Utils.tryUpdateBookmarkUrl(bookmarkId, recorder.bookmarkUrl)
      await this._updateBeforeRequestListener()
      await this._unredirectTabs(recorder.localUrl, closingTabId)
      return this._backend.stopRecording(recorder.recorderId)
    }
  }

  async _handleRecordingFinished (recorder) {
    //DEBUG('recording finished', recorder.url)
    delete this._recorderByBookmarkId[recorder.bookmarkId]
    for (const action of recorder.onFinished) {
      await action()
    }

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
    verify(localUrl)
    for (const recorder of this._recentRecorders) {
      if (localUrl.startsWith(recorder.localUrl)) {
        return recorder
      }
    }
  }

  _findRecorder (localUrl) {
    verify(localUrl)
    for (const bookmarkId in this._recorderByBookmarkId) {
      const recorder = this._recorderByBookmarkId[bookmarkId]
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

  async _reloadTabs (bookmarkId) {
    const recorder = this._recorderByBookmarkId[bookmarkId]
    const bookmark = await Utils.getBookmarkById(bookmarkId)
    const url = (recorder ? recorder.bookmarkUrl : bookmark.url)
    for (const tab of await Utils.findTabsMatchingUrl(url)) {
       await Utils.tryReloadTab(tab.id)
    }
  }

  async _unredirectTabs (localUrl, closingTabId) {
    for (const tab of await Utils.findTabsMatchingUrl(localUrl)) {
      if (tab.id !== closingTabId) {
        const url = this.getOriginalUrl(tab.url)
        await Utils.tryUpdateTabUrl(tab.id, url)
      }
    }
  }

  async _recorderHasTab (bookmarkId, closingTabId) {
    const recorder = this._recorderByBookmarkId[bookmarkId]
    for (const tab of await Utils.findTabsMatchingUrl(recorder.localUrl)) {
      if (tab.id !== closingTabId) {
        return true
      }
    }
    return false
  }

  async _stopRecordersWithoutTabs (closingTabId) {
    let recordersToStop = []
    for (const bookmarkId in this._recorderByBookmarkId) {
      if (!await this._recorderHasTab(bookmarkId, closingTabId)) {
        recordersToStop.push(bookmarkId)
      }
    }
    for (const bookmarkId of recordersToStop) {
      await this._stopRecording(bookmarkId, closingTabId)
    }
  }

  async _handleBookmarkCreated (bookmarkId, createInfo) {
    // translate to original url when bookmarking a local url
    const url = this.getOriginalUrl(createInfo.url)
    if (url !== createInfo.url) {
      await Utils.tryUpdateBookmarkUrl(bookmarkId, url)
    }
    await this._updateBeforeRequestListener()
    const { path, inLibrary } = await this._getBookmarkPath(bookmarkId)
    if (inLibrary) {
      await this._undeleteFile(bookmarkId)
    }
    return this._reloadTabs(bookmarkId)
  }

  async _handleBookmarkChanged (bookmarkId, changeInfo) {
    if (changeInfo.url) {
      await this._updateBeforeRequestListener()
    }
    if (changeInfo.title) {
      const bookmarkTitle = this._libraryBookmarkTitles[bookmarkId]
      verify(bookmarkTitle)
      const { path, inLibrary } = await this._getBookmarkPath(bookmarkId)
      if (inLibrary) {
        const sourcePath = path.slice()
        sourcePath[sourcePath.length - 1] = bookmarkTitle
        await this._moveFile(bookmarkId, sourcePath, path)
      }
    }
  }

  async _handleBookmarkMoved (bookmarkId, moveInfo) {
    const bookmarkTitle = this._libraryBookmarkTitles[bookmarkId]
    await this._updateBeforeRequestListener()
    const source = await this._getBookmarkPath(moveInfo.oldParentId)
    const target = await this._getBookmarkPath(moveInfo.parentId)
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
    await this._updateBeforeRequestListener()
    await this._stopRecording(bookmarkId)
    const source = await this._getBookmarkPath(removeInfo.parentId)
    source.path.push(bookmarkTitle)
    if (source.inLibrary) {
      await this._deleteFile(bookmarkId, source.path)
    }
  }

  async _handleBookmarkRequested (details) {
    if (details.tabId < 0) {
      return
    }
    const url = details.url
    const bookmark = await this._findBookmarkByUrl(url)
    if (!bookmark) {
      return
    }
    const recorder = this._recorderByBookmarkId[bookmark.id]
    if (recorder) {
      if (recorder.finishing) {
        return
      }
      if (!recorder.localUrl) {
        //DEBUG('cancelled request to', url)
        return { cancel: true }
      }
      //DEBUG('redirecting request to', recorder.localUrl)
      return { redirectUrl: this._getLocalUrl(url, recorder) }
    }

    //DEBUG('cancelled request to', url)
    this._startRecording(bookmark.id, url, details.tabId)
    return { cancel: true }
  }

  async _findBookmarkByUrl (url) {
    url = Utils.getHostnamePathWithoutWWW(url)
    for (const bookmarkId in this._recorderByBookmarkId) {
      const bookmark = this._recorderByBookmarkId[bookmarkId]
      if (url.startsWith(Utils.getHostnamePathWithoutWWW(bookmark.bookmarkUrl))) {
        return Utils.getBookmarkById(bookmarkId)
      }
    }
    for (const bookmark of await this._getBookmarks()) {
      if (bookmark.url &&
          url.startsWith(Utils.getHostnamePathWithoutWWW(bookmark.url))) {
        return bookmark
      }
    }
  }

  async _updateBeforeRequestListener () {
    const bookmarkTitles = { }
    const urlFilters = []
    for (const bookmark of await this._getBookmarks()) {
      bookmarkTitles[bookmark.id] = bookmark.title
      if (bookmark.url && !bookmark.url.startsWith('http://127.0.0.1')) {
        Utils.getUrlMatchPattern(bookmark.url, urlFilters)
      }
    }
    for (const bookmarkId in this._recorderByBookmarkId) {
      const recorder = this._recorderByBookmarkId[bookmarkId]
      Utils.getUrlMatchPattern(recorder.bookmarkUrl, urlFilters)
    }
    this._libraryBookmarkTitles = bookmarkTitles

    //DEBUG('updating url filters', urlFilters.length)
    await browser.webRequest.onBeforeRequest.removeListener(
      this._beforeRequestListener)
    if (urlFilters.length > 0) {
      await browser.webRequest.onBeforeRequest.addListener(
        this._beforeRequestListener, { urls: urlFilters }, ['blocking'])
    }
  }
}
