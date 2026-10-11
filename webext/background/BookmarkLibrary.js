'use strict'
/* global browser, Utils, verify, DEBUG_LOG_ASYNC_CALLS */

class BookmarkLibrary {
  constructor (backend) {
    this._backend = backend
    this._rootId = null
    this._recentRecorders = []
    this._libraryBookmarks = []
    this._libraryBookmarkTitles = { }
    this._catalogueById = new Map()
    this._catalogueByPath = new Map()
    this._syncedCatalogueByPath = new Map()
    this._bypassHosts = { }
    this._temporarilyBypassedBookmarkIds = []
    this._reconciledBookmarkIds = new Set()
    this._updates = Promise.resolve()
    this._ready = new Promise(resolve => { this._resolveReady = resolve })

    browser.bookmarks.onCreated.addListener((id, info) => {
      this._ready.then(() => this._queueUpdate(
        () => this._handleBookmarkCreated(id, info))).catch(console.error)
    })
    browser.bookmarks.onChanged.addListener((id, info) => {
      this._ready.then(() => this._queueUpdate(
        () => this._handleBookmarkChanged(id, info))).catch(console.error)
    })
    browser.bookmarks.onRemoved.addListener((id, info) => {
      this._ready.then(() => this._queueUpdate(
        () => this._handleBookmarkRemoved(id, info))).catch(console.error)
    })
    browser.bookmarks.onMoved.addListener((id, info) => {
      this._ready.then(() => this._queueUpdate(
        () => this._handleBookmarkMoved(id, info))).catch(console.error)
    })
    browser.tabs.onRemoved.addListener(tabId =>
      this._ready.then(() => this._queueUpdate(
        () => this._handleTabRemoved(tabId))).catch(console.error))
    if (browser.proxy.onRequest) {
      browser.proxy.onRequest.addListener(details => this._routeProxy(details),
        { urls: ['http://*/*', 'https://*/*'] })
    }
  }

  async setRootId (rootId) {
    verify(rootId)
    if (this._rootId !== rootId) {
      if (this._rootId) await this.stopAll()
      this._rootId = rootId
      await this._refreshLibraryBookmarkList(false)
      await this._restoreRecentRecorders()
      await this._refreshLibraryBookmarkList(false)
      await this._reloadRecentRecorderTabs()
    }
    this._resolveReady()
  }

  async stopAll () {
    await this._backend.finishAllRecordings()
  }

  get rootId () {
    return this._rootId
  }

  async updateRecordingOptions () {
    if (this._rootId) await this._backend.updateRecordingOptions()
  }

  async setBypassHosts (hostList) {
    const hosts = { }
    hostList
      .split('\n')
      .map(line => line.trim())
      .filter(line => line.length > 0 && line.indexOf(';') !== 0)
      .forEach(host => { hosts[host] = true })
    this._bypassHosts = hosts
    // Bypassed hosts still forward through the proxy, without an archive session.
    if (this._rootId) await this._refreshLibraryBookmarkList()
  }

  async temporarilyBypassBookmark (bookmarkId) {
    const entry = this._catalogueById.get(bookmarkId)
    if (!this._temporarilyBypassedBookmarkIds.includes(bookmarkId)) {
      this._temporarilyBypassedBookmarkIds.push(bookmarkId)
      await this._refreshLibraryBookmarkList()
    }
    if (entry) await this._backend.finishRecording(entry.path)
    return true
  }

  _queueUpdate (action) {
    const update = this._updates.catch(() => {}).then(action)
    this._updates = update
    return update
  }

  getOriginalUrl (url) {
    if (Utils.isLocalUrl(url)) {
      // Migrate URLs left over by the old redirecting extension.
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

  async findBookmarkByUrl (url) {
    for (const bookmark of this._libraryBookmarks) {
      if (Utils.isHttpUrl(bookmark.url) &&
          this._bookmarkMatchesUrl(bookmark.url, url) &&
          this._temporarilyBypassedBookmarkIds.indexOf(bookmark.id) === -1) {
        return bookmark
      }
    }
  }

  _bookmarkMatchesUrl (bookmarkUrl, url) {
    if (!Utils.isHttpUrl(bookmarkUrl) || !Utils.isHttpUrl(url)) return false
    const target = new URL(url)
    const base = new URL(bookmarkUrl)
    if (target.host.replace(/^www\./, '') !== base.host.replace(/^www\./, '')) return false
    const targetPath = target.pathname + target.search
    const basePath = base.pathname + base.search
    if (targetPath === basePath) return true
    if (!targetPath.startsWith(basePath)) return false
    return basePath.endsWith('/') || ['/', '?'].includes(targetPath[basePath.length])
  }

  async _findBookmarksWithOpenTabs (excludedTabId) {
    const tabs = (await browser.tabs.query({})).filter(tab => tab.id !== excludedTabId)
    return Array.from(this._catalogueByPath.values())
      .filter(bookmark => tabs.some(tab => this._bookmarkMatchesUrl(bookmark.url, tab.url)))
      .map(bookmark => bookmark.path)
  }

  async _purgeTemporarilyBypassedBookmarks () {
    for (let i = 0; i < this._temporarilyBypassedBookmarkIds.length;) {
      const tabs = await this._findTabsByBookmarkId(this._temporarilyBypassedBookmarkIds[i])
      if (tabs.length > 0) {
        ++i
      } else {
        this._temporarilyBypassedBookmarkIds.splice(i, 1)
      }
    }
  }

  async _collectLibraryBookmarks () {
    if (!this._rootId) {
      return []
    }
    const bookmarks = []
    const rec = function (bookmark) {
      for (const child of bookmark.children) {
        if (!child.url) {
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

  async _refreshLibraryBookmarkList (synchronize = true) {
    const libraryBookmarks = await this._collectLibraryBookmarks()
    const bookmarkTitles = { }
    for (const bookmark of libraryBookmarks) {
      bookmarkTitles[bookmark.id] = bookmark.title
    }
    const catalogueById = new Map()
    const catalogueByPath = new Map()
    for (const bookmark of libraryBookmarks) {
      if (!Utils.isHttpUrl(bookmark.url)) continue
      const { path, inLibrary } = await this.getBookmarkPath(bookmark.id)
      if (inLibrary && !this._temporarilyBypassedBookmarkIds.includes(bookmark.id) &&
          !this._shouldBypassHost(bookmark.url)) {
        const entry = {
          id: bookmark.id,
          url: new URL(bookmark.url).href,
          path,
          archivePath: this._backend.archivePath(path)
        }
        catalogueById.set(bookmark.id, entry)
        if (!catalogueByPath.has(entry.archivePath)) catalogueByPath.set(entry.archivePath, entry)
      }
    }
    this._libraryBookmarks = libraryBookmarks
    this._libraryBookmarkTitles = bookmarkTitles
    this._catalogueById = catalogueById
    this._catalogueByPath = catalogueByPath
    if (!synchronize) {
      this._syncedCatalogueByPath = new Map(catalogueByPath)
      return
    }

    const removed = []
    const upserts = []
    for (const path of this._syncedCatalogueByPath.keys()) {
      if (!catalogueByPath.has(path)) removed.push(path)
    }
    for (const [path, bookmark] of catalogueByPath) {
      const previous = this._syncedCatalogueByPath.get(path)
      if (!previous || previous.url !== bookmark.url) upserts.push(bookmark)
    }
    if (removed.length || upserts.length) {
      await this._backend.updateBookmarkCatalog(removed, upserts)
      this._syncedCatalogueByPath = new Map(catalogueByPath)
    }
  }

  async _ensureLibraryBookmark (archivePath, url) {
    const parts = archivePath.replace(/\\/g, '/').split('/').filter(Boolean)
    if (!parts.length) return false
    let parentId = this._rootId
    for (const title of parts.slice(0, -1)) {
      const children = await browser.bookmarks.getChildren(parentId)
      let folder = children.find(child => !child.url && child.title === title)
      if (!folder) folder = await browser.bookmarks.create({ parentId, title })
      parentId = folder.id
    }
    const title = parts[parts.length - 1]
    const children = await browser.bookmarks.getChildren(parentId)
    if (children.some(child => child.url && child.title === title)) return false
    const bookmark = await browser.bookmarks.create({ parentId, title, url })
    this._reconciledBookmarkIds.add(bookmark.id)
    return true
  }

  async _synchronizeAll () {
    const listing = await this._backend.getLibraryListing()
    await this._refreshLibraryBookmarkList(false)
    const browserPaths = new Set()
    for (const bookmark of this._libraryBookmarks) {
      if (!Utils.isHttpUrl(bookmark.url)) continue
      const location = await this.getBookmarkPath(bookmark.id)
      if (location.inLibrary) browserPaths.add(this._backend.archivePath(location.path))
    }
    let created = 0
    for (const file of listing.files || []) {
      const archivePath = String(file.filename).replace(/\\/g, '/')
      if (browserPaths.has(archivePath)) continue
      if (await this._ensureLibraryBookmark(archivePath, file.url)) {
        browserPaths.add(archivePath)
        ++created
      }
    }
    await this._refreshLibraryBookmarkList(false)
    await this._backend.syncBookmarkCatalog(Array.from(this._catalogueByPath.values()))
    this._syncedCatalogueByPath = new Map(this._catalogueByPath)
    return {
      created,
      bookmarks: this._catalogueByPath.size,
      archives: (listing.files || []).length
    }
  }

  synchronizeAll () {
    return this._queueUpdate(() => this._synchronizeAll())
  }

  async _restoreRecentRecorders () {
    this._recentRecorders = await Utils.getSetting('recent-recorders', [])
    // undo patching bookmark url (just in case browser crashed)
    for (const bookmark of this._libraryBookmarks) {
      for (const recentRecorder of this._recentRecorders) {
        if (bookmark.url && bookmark.url.startsWith(recentRecorder.localOrigin)) {
          await Utils.tryUpdateBookmarkUrl(bookmark.id, recentRecorder.bookmarkUrl)
        }
      }
    }
  }

  async _reloadRecentRecorderTabs () {
    for (const tab of await browser.tabs.query({})) {
      for (const recentRecorder of this._recentRecorders) {
        if (tab.url.startsWith(recentRecorder.localOrigin)) {
          await browser.tabs.update(tab.id, { url: this.getOriginalUrl(tab.url) })
        }
      }
    }
  }

  _findRecentRecorder (localUrl) {
    verify(Utils.isLocalUrl(localUrl))
    for (const recentRecorder of this._recentRecorders) {
      if (localUrl.startsWith(recentRecorder.localOrigin)) {
        return recentRecorder
      }
    }
  }

  async _handleTabRemoved (tabId) {
    // Firefox can briefly retain the closing tab in a tabs.query() result while
    // dispatching onRemoved. Excluding the event's tab ID makes last-tab closure
    // deterministic in every browser.
    const activePaths = await this._findBookmarksWithOpenTabs(tabId)
    await this._backend.finishUnlistedRecordings(activePaths)
    await this._purgeTemporarilyBypassedBookmarks()
    await this._refreshLibraryBookmarkList()
  }

  async _callOnRecordingFinished (path, action) {
    await this._backend.finishRecording(path)
    return action()
  }

  async _moveFile (bookmarkId, sourcePath, targetPath) {
    return this._callOnRecordingFinished(sourcePath,
      () => this._backend.moveFile(sourcePath, targetPath))
  }

  async _deleteFile (bookmarkId, path) {
    const undeleteId = bookmarkId
    return this._callOnRecordingFinished(path,
      () => this._backend.deleteFile(path, undeleteId))
  }

  async _undeleteFile (bookmarkId, path) {
    const undeleteId = bookmarkId
    return this._callOnRecordingFinished(path,
      () => this._backend.undeleteFile(undeleteId))
  }

  async _findTabsByBookmarkId (bookmarkId) {
    const bookmark = await Utils.getBookmarkById(bookmarkId)
    return Utils.findTabsMatchingUrl(bookmark.url)
  }

  async _reloadTabs (bookmarkId, excludeTabId) {
    for (const tab of await this._findTabsByBookmarkId(bookmarkId)) {
      if (tab.id !== excludeTabId) {
        await Utils.tryReloadTab(tab.id)
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
    await this._refreshLibraryBookmarkList()
    if (!createInfo.url) return
    const { path, inLibrary } = await this.getBookmarkPath(bookmarkId)
    if (inLibrary) {
      if (this._reconciledBookmarkIds.delete(bookmarkId)) return this._reloadTabs(bookmarkId)
      await this._undeleteFile(bookmarkId, path)
      return this._reloadTabs(bookmarkId)
    }
  }

  async _handleBookmarkChanged (bookmarkId, changeInfo) {
    const bookmarkTitle = this._libraryBookmarkTitles[bookmarkId]
    await this._refreshLibraryBookmarkList()
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
    await this._refreshLibraryBookmarkList()
    const source = await this.getBookmarkPath(moveInfo.oldParentId)
    const target = await this.getBookmarkPath(moveInfo.parentId)
    if (source.inLibrary) {
      source.path.push(bookmarkTitle)
      target.path.push(bookmarkTitle)
      if (target.inLibrary) {
        return this._moveFile(bookmarkId, source.path, target.path)
      }
      return this._deleteFile(bookmarkId, source.path)
    }
    if (target.inLibrary) {
      await this._reloadTabs(bookmarkId)
      return this._undeleteFile(bookmarkId, target.path)
    }
  }

  async _handleBookmarkRemoved (bookmarkId, removeInfo) {
    const bookmarkTitle = this._libraryBookmarkTitles[bookmarkId]
    await this._refreshLibraryBookmarkList()
    const source = await this.getBookmarkPath(removeInfo.parentId)
    source.path.push(bookmarkTitle)
    if (source.inLibrary) {
      await this._deleteFile(bookmarkId, source.path)
    }
  }

  _shouldBypassHost (url) {
    let host = new URL(url).host
    for (;;) {
      if (this._bypassHosts[host]) {
        return true
      }
      const dot = host.indexOf('.')
      if (dot === -1) {
        return false
      }
      host = host.substring(dot + 1)
    }
  }

  async _routeProxy (details) {
    if (Utils.isLocalUrl(details.url) || this._backend.isControlUrl(details.url)) {
      return { type: 'direct' }
    }
    try {
      const proxy = await this._backend.ensureProxy()
      return { type: 'http', host: proxy.proxyHost || '127.0.0.1', port: proxy.proxyPort }
    } catch (error) {
      console.error('Starting proxy failed', error)
      return { type: 'direct' }
    }
  }
}

DEBUG_LOG_ASYNC_CALLS(BookmarkLibrary.prototype)
