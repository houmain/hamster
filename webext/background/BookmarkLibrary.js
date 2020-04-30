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
    this._bypassHosts = { }

    browser.bookmarks.onCreated.addListener((id, info) => this._handleBookmarkCreated(id, info))
    browser.bookmarks.onChanged.addListener((id, info) => this._handleBookmarkChanged(id, info))
    browser.bookmarks.onRemoved.addListener((id, info) => this._handleBookmarkRemoved(id, info))
    browser.bookmarks.onMoved.addListener((id, info) => this._handleBookmarkMoved(id, info))
    browser.tabs.onRemoved.addListener((id) => this._stopRecordingInTab(id))
    browser.webRequest.onBeforeRequest.addListener(
      async (details) => this._handleBeforeRequest(details),
      { urls: [ 'http://*/*', 'https://*/*' ] }, [ 'blocking' ])
  }

  async setRootId (rootId) {
    verify(rootId)
    if (this._rootId != rootId) {
      this._rootId = rootId
      await this._restoreRecentRecorders()
      await backend.injectScript(`(${injectScript})(document)`)
      return this._updateLibraryBookmarkList()
    }
  }

  get rootId () {
    return this._rootId
  }

  setBypassHosts (hostList) {
    const hosts = { }
    hostList
      .split('\n')
      .map(line => line.trim())
      .filter(line => line.length > 0 && line.indexOf(';') !== 0)
      .forEach(host => hosts[host] = true)
    this._bypassHosts = hosts
  }

  getOriginalUrl (url) {
    if (Utils.isHttpUrl(url)) {
      // try to unpatch
      const pathQuery = Utils.getPathQuery(url)
      if (Utils.isHttpUrl(pathQuery.substring(1))) {
        return pathQuery.substring(1)
      }
      // try to translate from local url to original url
      if (Utils.isLocalUrl(url)) {
        const recentRecorder = this._findRecentRecorder(url)
        if (recentRecorder) {
          return recentRecorder.origin + pathQuery
        }
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

  async findBookmarkByUrl (url) {
    url = Utils.getHostPathWithoutWWW(url)
    for (const bookmarkId in this._recorderByBookmarkId) {
      const recorder = this._recorderByBookmarkId[bookmarkId]
      if (!recorder.finishing &&
          url.startsWith(Utils.getHostPathWithoutWWW(recorder.bookmarkUrl))) {
        return Utils.getBookmarkById(bookmarkId)
      }
    }
    for (const bookmark of await this._getBookmarks()) {
      if (Utils.isHttpUrl(bookmark.url) &&
          url.startsWith(Utils.getHostPathWithoutWWW(bookmark.url))) {
        return bookmark
      }
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
    verify(bookmarkId, initialTabId)
    verify(Utils.isHttpUrl(url), !Utils.isLocalUrl(url))
    verify(!this._recorderByBookmarkId[bookmarkId])
    verify(!this._recorderByTabId[initialTabId])

    //DEBUG('start recording', url)
    const bookmark = await Utils.getBookmarkById(bookmarkId)
    const recorder = {
      recorderId: this._nextRecorderId++,
      url: new URL(url),
      bookmarkId: bookmark.id,
      bookmarkUrl: bookmark.url,
      localOrigin: null,
      localHost: null,
      localHostname: null,
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

    let recorder = this._recorderByBookmarkId[bookmarkId]
    if (recorder && recorder.finishing) {
      while (recorder.finishing) {
        await Utils.sleep(10)
      }
      verify(!this._recorderByBookmarkId[bookmarkId])
      recorder = null
    }

    if (!recorder) {
      return this._startRecording(bookmarkId, url, tabId)
    }

    verify(recorder.tabIds.indexOf(tabId) === -1)
    this._recorderByTabId[tabId] = recorder
    recorder.tabIds.push(tabId)
    return recorder
  }

  async _handleRecordingOutput (recorder, event) {
    if (!event) {
      await this._handleRecordingFinished(recorder)
    } else if (event.startsWith('ACCEPT ')) {
      await this._handleRecordingStarted(recorder, event.substring(7))
    } else if (event.startsWith('REDIRECT ')) {
      await this._handleRecordingRedirected(recorder, event.substring(9))
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
    //DEBUG('recording started', recorder.url.href, 'at', localUrl)
    await Utils.tryUpdateBookmarkUrl(recorder.bookmarkId, localUrl)
    await this._updateLibraryBookmarkList()
    recorder.localUrl = new URL(localUrl)
    await this._updateRecentRecorders(recorder)
    await this._reloadTabs(recorder.bookmarkId, recorder.tabIds[0])
  }

  async _handleRecordingRedirected (recorder, url) {
    verify(Utils.isHttpUrl(url), !Utils.isLocalUrl(url))
    //DEBUG('recording redirected', recorder.url, 'to', url)
    recorder.url = new URL(url)
    await this._updateRecentRecorders(recorder)
  }

  async _restoreRecentRecorders () {
    this._recentRecorders = await Utils.getSetting('recent-recorders', [])

    // undo patching bookmark url (just in case browser crashed)
    for (const bookmark of await this._getBookmarks()) {
      for (const recentRecorder of this._recentRecorders) {
        if (bookmark.url && bookmark.url.startsWith(recentRecorder.localOrigin)) {
          await Utils.tryUpdateBookmarkUrl(bookmark.id, recentRecorder.bookmarkUrl)
        }
      }
    }
  }

  async _updateRecentRecorders (recorder) {
    //DEBUG('updating recent recorders', recorder.localUrl.origin, recorder.url.origin)
    this._recentRecorders.unshift({
      localOrigin: recorder.localUrl.origin,
      bookmarkUrl: recorder.bookmarkUrl,
      origin: recorder.url.origin
    })
    while (this._recentRecorders.length > 20) {
      this._recentRecorders.pop()
    }
    return Utils.setSetting('recent-recorders', this._recentRecorders)
  }

  _findRecentRecorder (localUrl) {
    verify(Utils.isLocalUrl(localUrl))
    for (const recentRecorder of this._recentRecorders) {
      if (localUrl.startsWith(recentRecorder.localOrigin)) {
        return recentRecorder
      }
    }
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

  async _findTabsByBookmarkId (bookmarkId) {
    const recorder = this._recorderByBookmarkId[bookmarkId]
    const bookmark = await Utils.getBookmarkById(bookmarkId)
    const url = (recorder ? recorder.bookmarkUrl : bookmark.url)
    return Utils.findTabsMatchingUrl(url)
  }

  async _reloadTabs (bookmarkId, excludeTabId) {
    for (let tab of await this._findTabsByBookmarkId(bookmarkId)) {
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
      return this._reloadTabs(bookmarkId)
    }
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
    if (target.inLibrary) {
      await this._reloadTabs(bookmarkId)
      return this._undeleteFile(bookmarkId)
    }
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
    verify(recorder, recorder.localUrl)
    if (!Utils.isHttpUrl(url) || Utils.isLocalUrl(url)) {
      return url;
    }

    // replace [http://]127.0.0.1[:port] in the middle
    if (url.indexOf(encodeURIComponent(recorder.localUrl.hostname) >= 0)) {
      url = url.split(encodeURIComponent(recorder.localUrl.origin)).join(
        encodeURIComponent(recorder.url.origin))
      url = url.split(encodeURIComponent(recorder.localUrl.host)).join(
        encodeURIComponent(recorder.url.host))
      url = url.split(encodeURIComponent(recorder.localUrl.hostname)).join(
        encodeURIComponent(recorder.url.hostname))
    }

    // convert to local url
    if (Utils.getOrigin(url) == recorder.url.origin) {
      return recorder.localUrl.origin + Utils.getPathQuery(url)
    }
    return recorder.localUrl.origin + '/' + url
  }

  async _handleBeforeRequest (details) {
    const { url, documentUrl, tabId, type } = details
    if (tabId < 0) {
      //DEBUG('ignoring request not in a tab', url)
      return
    }

    let recorder = this._recorderByTabId[tabId]
    if (type === 'main_frame') {
      const originalUrl = this.getOriginalUrl(url)
      const bookmark = await this.findBookmarkByUrl(originalUrl)

      if (Utils.isLocalUrl(url) && !bookmark && originalUrl !== url) {
        // restore original url of local url not belonging to a bookmark
        //DEBUG('redirecting request', url, 'to', originalUrl)
        return { redirectUrl: originalUrl }
      }

      if (recorder) {
        // stop recording when navigating away
        if (!bookmark || this._recorderByBookmarkId[bookmark.id] !== recorder) {
          await this._stopRecordingInTab(tabId)
          recorder = null
        }
      }

      if (bookmark && !recorder && !Utils.isLocalUrl(originalUrl)) {
        // start recording
        recorder = await this._startRecordingInTab(bookmark.id, originalUrl, tabId)
        while (!recorder.localUrl) {
          await Utils.sleep(10)
        }
      }

      if (recorder) {
        const patchedUrl = this._patchUrl(originalUrl, recorder)
        if (url !== patchedUrl) {
          //DEBUG('redirecting request', url, 'to', patchedUrl)
          return { redirectUrl: patchedUrl }
        }
      }
      //DEBUG('passing request to', url)
    }
    else if (Utils.isLocalUrl(documentUrl) && !Utils.isLocalUrl(url) && recorder) {
      // redirect resources to recorder

      const host = new URL(url).host
      if (this._bypassHosts[host]) {
        //DEBUG('bypassed', url)
        return
      }

      if (recorder.finishing) {
        //DEBUG('cancelling resource request', url)
        return { cancel: true }
      }
      const patchedUrl = this._patchUrl(url, recorder)
      //DEBUG('redirecting resource request', url, 'to', patchedUrl)
      return { redirectUrl: patchedUrl }
    }
  }
}
