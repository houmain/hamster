'use strict'
/* global browser, Utils, DEBUG, NativeClient, Backend, BookmarkLibrary */

const NATIVE_CLIENT_ID = 'hamster'
const BACKEND_NOTIFICATION_ID = 'hamster-backend-help'
const BACKEND_MONITOR_ALARM = 'hamster-backend-monitor'
const STARTUP_SYNCHRONIZATION_DELAY_MS = 5000
const MENU_ROOT_ID = 'menu-root'
const MENU_FILE_LISTING_ID = 'menu-file-listing'
const MENU_OPEN_ORIGINAL_ID = 'menu-open-original'
const MENU_OPTIONS_SEPARATOR_ID = 'menu-options-separator'
const MENU_SEARCH_ID = 'menu-search'
const MENU_OPTIONS_ID = 'menu-options'
const DEFAULT_BYPASS_HOSTS_LIST =
`youtube.com
vimeo.com
digiteka.net
`
let backendNotificationState = null
let backendNotificationUpdate = Promise.resolve()

const nativeClient = new NativeClient(NATIVE_CLIENT_ID)
const backend = new Backend(nativeClient)
const bookmarkLibrary = new BookmarkLibrary(backend)
const menus = browser.menus || browser.contextMenus

async function createDefaultBookmarkRoot () {
  const title = browser.i18n.getMessage('default_bookmark_root_title')
  const bases = await Utils.getBookmarkBaseFolders()
  for (const base of bases) {
    const children = await browser.bookmarks.getChildren(base.id)
    const child = children.find(c => !c.url && c.title === title)
    if (child) return child.id
  }
  if (!bases.length) throw new Error('The browser has no writable bookmark folder')
  const root = await browser.bookmarks.create({ parentId: bases[0].id, title })
  if (!root?.id) throw new Error('Creating the Bookmark Hamster folder failed')
  return root.id
}

async function initializeBookmarkRoot () {
  try {
    const rootId = await Utils.getSetting('bookmark-root-id')
    if (rootId) {
      await Utils.getBookmarkById(rootId)
      return rootId
    }
  } catch (ex) {
    DEBUG('initializing bookmark root failed: ', ex)
  }
  const rootId = await createDefaultBookmarkRoot()
  await Utils.setSetting('bookmark-root-id', rootId)
  return rootId
}

async function restoreOptions () {
  try {
    await Utils.setDefaultSetting('default-serve-mode', 'latest')
    await Utils.setDefaultSetting('allow-lossy-compression', true)
    await Utils.setDefaultSetting('bypass-hosts', DEFAULT_BYPASS_HOSTS_LIST)
    await browser.storage.local.remove(['catalog-client-id', 'catalog-revision'])
    await bookmarkLibrary.setBypassHosts(await Utils.getSetting('bypass-hosts', ''))
    await backend.configuration()
    await bookmarkLibrary.setRootId(await initializeBookmarkRoot())
  } catch (ex) {
    DEBUG('restoring options failed: ', ex)
  }
}

function escapeOmniboxDescription (text) {
  const entities = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&apos;' }
  return String(text).replace(/[&<>"']/g, character => entities[character])
}

function createSuggestions (response) {
  return response.matches.map(match => ({
    content: match.url,
    description: escapeOmniboxDescription(match.snippet)
  }))
}

function handleOmniBoxInput (text, addSuggestions) {
  browser.omnibox.setDefaultSuggestion({
    description: escapeOmniboxDescription(browser.i18n.getMessage('omnibox_default_search'))
  })
  initialization.then(() => backend.executeSearch(text))
    .then(createSuggestions).then(addSuggestions).catch(() => addSuggestions([]))
}

function openPage (url, disposition) {
  switch (disposition) {
    case 'currentTab': browser.tabs.update({ url }); break
    case 'newBackgroundTab': browser.tabs.create({ url, active: false }); break
    default: browser.tabs.create({ url }); break
  }
}

function handleOmniBoxSelection (url, disposition) {
  if (!/^https?:/i.test(url)) {
    url = browser.runtime.getURL('search/search.html') + '?s=' + encodeURIComponent(url)
  }
  openPage(url, disposition)
}

function handleSearchMenuClicked () {
  openPage(browser.runtime.getURL('search/search.html'))
}

async function getContextBookmark (info) {
  if (Utils.isHttpUrl(info.pageUrl)) return bookmarkLibrary.findBookmarkByUrl(info.pageUrl)
  if (info.bookmarkId) return Utils.getBookmarkById(info.bookmarkId)
}

async function handleFileListingMenuClicked (info) {
  const bookmark = await getContextBookmark(info)
  if (bookmark) browser.tabs.create({ url: browser.runtime.getURL('listing/listing.html?id=' + bookmark.id) })
}

async function openOriginal (bookmarkId, pageUrl) {
  if (await bookmarkLibrary.temporarilyBypassBookmark(bookmarkId)) {
    await browser.tabs.create({ url: bookmarkLibrary.getOriginalUrl(pageUrl) })
  }
}

async function handleOpenOriginalMenuClicked (info) {
  const bookmark = await getContextBookmark(info)
  if (bookmark) await openOriginal(bookmark.id, info.pageUrl || bookmark.url)
}

function handleOptionsMenuClicked () { browser.runtime.openOptionsPage() }

function handleNotificationClicked (id) {
  if (id === BACKEND_NOTIFICATION_ID) handleOptionsMenuClicked()
}

function updateBackendNotification (status) {
  const state = status.valid ? '' : `${status.state}:${status.errorMessage}`
  if (backendNotificationState === state) return backendNotificationUpdate
  backendNotificationState = state
  backendNotificationUpdate = backendNotificationUpdate.catch(error => {
    console.error('Updating Hamster proxy notification failed', error)
  }).then(async () => {
    if (state !== backendNotificationState) return
    if (status.valid) {
      await browser.notifications.clear(BACKEND_NOTIFICATION_ID)
      return
    }
    await browser.notifications.create(BACKEND_NOTIFICATION_ID, {
      type: 'basic',
      iconUrl: browser.runtime.getURL('icons/icon128.png'),
      title: browser.i18n.getMessage('notification_no_backend_title'),
      message: browser.i18n.getMessage(status.errorMessage)
    })
  })
  return backendNotificationUpdate
}

async function updateBrowserProxy (status) {
  // Firefox's global proxy setting requires separate private-window access.
  // Its per-request hook avoids that install-time requirement.
  if (browser.proxy.onRequest) return
  if (!status.valid) {
    await browser.proxy.settings.clear({ scope: 'regular' })
    return
  }
  const controlHost = new URL(status.origin).hostname
  const bypassList = ['localhost', '127.0.0.1', '[::1]', controlHost]
  await browser.proxy.settings.set({
    scope: 'regular',
    value: {
      mode: 'fixed_servers',
      rules: {
        singleProxy: {
          scheme: 'http',
          host: status.proxy.proxyHost,
          port: status.proxy.proxyPort
        },
        bypassList
      }
    }
  })
}

async function handleBackendStatus (status) {
  try { await updateBrowserProxy(status) } catch (error) {
    console.error('Updating browser proxy settings failed', error)
  }
  // Firefox utility windows, such as the Downloads/Library window, can keep
  // the extension background alive after the last browser window is closed.
  // The local proxy is deliberately stopped in that state, so its absence is
  // not an error worth notifying about.
  const hasBrowserWindows = (await browser.windows.getAll()).length > 0
  await updateBackendNotification(hasBrowserWindows ? status : { valid: true })
}

async function monitorBackend () {
  await initialization
  if (!(await browser.windows.getAll()).length) {
    await updateBackendNotification({ valid: true })
    return
  }
  try { await backend.checkConnection() } catch (error) {
    console.error('Checking Hamster proxy failed', error)
  }
}

function scheduleStartupSynchronization () {
  setTimeout(() => {
    bookmarkLibrary.synchronizeAll().catch(error => {
      console.error('Synchronizing Hamster bookmarks at startup failed', error)
    })
  }, STARTUP_SYNCHRONIZATION_DELAY_MS)
}

async function handleWindowRemoved () {
  if ((await browser.windows.getAll()).length) return
  // Closing the last browser window ends this browser's ownership lease even
  // when Chrome or Edge remains resident for background applications.
  await updateBackendNotification({ valid: true })
  await bookmarkLibrary.stopAll().catch(error => {
    console.error('Flushing Hamster recordings failed', error)
  })
  nativeClient.disconnect()
}

function menuContexts () { return browser.menus ? ['bookmark', 'tab'] : ['page'] }

async function createMenus () {
  if (!menus) return
  await menus.removeAll()
  const contexts = menuContexts()
  menus.create({ id: MENU_ROOT_ID, contexts, title: browser.i18n.getMessage('menu_root') })
  menus.create({ id: MENU_OPEN_ORIGINAL_ID, parentId: MENU_ROOT_ID, contexts,
    title: browser.i18n.getMessage('menu_open_original') })
  menus.create({ id: MENU_FILE_LISTING_ID, parentId: MENU_ROOT_ID, contexts,
    title: browser.i18n.getMessage('menu_file_listing') })
  menus.create({ id: MENU_OPTIONS_SEPARATOR_ID, parentId: MENU_ROOT_ID, type: 'separator', contexts })
  menus.create({ id: MENU_SEARCH_ID, parentId: MENU_ROOT_ID, contexts,
    title: browser.i18n.getMessage('menu_search') })
  menus.create({ id: MENU_OPTIONS_ID, parentId: MENU_ROOT_ID, contexts,
    title: browser.i18n.getMessage('menu_options') })
  if (menus.refresh) await menus.refresh()
}

async function handleMenuClicked (info) {
  await initialization
  switch (info.menuItemId) {
    case MENU_OPEN_ORIGINAL_ID: return handleOpenOriginalMenuClicked(info)
    case MENU_FILE_LISTING_ID: return handleFileListingMenuClicked(info)
    case MENU_SEARCH_ID: return handleSearchMenuClicked()
    case MENU_OPTIONS_ID: return handleOptionsMenuClicked()
  }
}

async function handleMenuShown (info) {
  let bookmark
  if (info.contexts.includes('bookmark') && info.bookmarkId) {
    bookmark = await Utils.getBookmarkById(info.bookmarkId)
  } else if (Utils.isHttpUrl(info.pageUrl)) {
    bookmark = await bookmarkLibrary.findBookmarkByUrl(info.pageUrl)
  }
  if (bookmark) {
    const isFolder = !bookmark.url
    const { inLibrary } = await bookmarkLibrary.getBookmarkPath(bookmark.id)
    await menus.update(MENU_ROOT_ID, { visible: inLibrary })
    await menus.update(MENU_OPEN_ORIGINAL_ID, { visible: !isFolder })
    await menus.update(MENU_FILE_LISTING_ID, { visible: !isFolder })
    await menus.update(MENU_OPTIONS_SEPARATOR_ID, { visible: !isFolder })
  } else {
    await menus.update(MENU_ROOT_ID, { visible: false })
  }
  if (menus.refresh) await menus.refresh()
}

async function optionsState (checkConnection) {
  await initialization
  await backend.configuration()
  const root = await Utils.getBookmarkById(bookmarkLibrary.rootId)
  const status = checkConnection ? await backend.checkConnection() : backend.lastConnectionStatus
  return {
    serverHostname: backend.serverHostname,
    serverPort: backend.serverPort,
    serverAddress: backend.serverAddress,
    rootId: bookmarkLibrary.rootId,
    rootParentId: root.parentId,
    rootTitle: root.title,
    defaultServeMode: await Utils.getSetting('default-serve-mode'),
    allowLossyCompression: await Utils.getSetting('allow-lossy-compression'),
    bypassHosts: await Utils.getSetting('bypass-hosts'),
    status
  }
}

async function activePageState () {
  const tab = await Utils.getActiveTab()
  if (!tab) return { url: '', title: '', bookmark: null }
  const bookmark = Utils.isHttpUrl(tab.url)
    ? await bookmarkLibrary.findBookmarkByUrl(tab.url)
    : null
  return {
    url: tab.url || '',
    title: tab.title || '',
    bookmark: bookmark ? { id: bookmark.id, title: bookmark.title, url: bookmark.url } : null
  }
}

const rpcMethods = {
  getOptionsState: optionsState,
  getActivePageState: activePageState,
  openOriginal,
  async setConnection (hostname, port) {
    await bookmarkLibrary.stopAll().catch(() => {})
    await backend.setConnection(hostname, port)
    await restoreOptions()
    return optionsState(true)
  },
  async moveBookmarkRoot (parentId) {
    await browser.bookmarks.move(bookmarkLibrary.rootId, { parentId })
    return optionsState(false)
  },
  async renameBookmarkRoot (title) {
    await browser.bookmarks.update(bookmarkLibrary.rootId, { title })
    return optionsState(false)
  },
  async setServeMode (value) {
    await bookmarkLibrary.stopAll().catch(() => {})
    await Utils.setSetting('default-serve-mode', value)
    await bookmarkLibrary.updateRecordingOptions()
  },
  async setAllowLossyCompression (value) {
    await bookmarkLibrary.stopAll().catch(() => {})
    await Utils.setSetting('allow-lossy-compression', value)
    await bookmarkLibrary.updateRecordingOptions()
  },
  async setBypassHosts (value) {
    await Utils.setSetting('bypass-hosts', value)
    await bookmarkLibrary.setBypassHosts(value)
  },
  rebuildSearchIndex: () => backend.rebuildSearchIndex(),
  getIndexStatus: () => backend.getIndexStatus(),
  getBookmarkPath: id => bookmarkLibrary.getBookmarkPath(id),
  getFileListing: path => backend.getFileListing(path),
  executeSearch: (query, forSearchPage) => backend.executeSearch(query, forSearchPage)
}

async function handleRpc (message) {
  await initialization
  const method = rpcMethods[message.method]
  if (!method) throw new Error(`Unknown Hamster extension method: ${message.method}`)
  return method(...(message.args || []))
}

browser.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message?.target !== 'hamster-background') return false
  handleRpc(message).then(
    value => sendResponse({ ok: true, value }),
    error => sendResponse({ ok: false, error: error.message || String(error) })
  )
  return true
})

browser.omnibox.onInputChanged.addListener(handleOmniBoxInput)
browser.omnibox.onInputEntered.addListener(handleOmniBoxSelection)
browser.notifications.onClicked.addListener(handleNotificationClicked)
browser.runtime.onStartup.addListener(() => { monitorBackend().catch(console.error) })
browser.windows.onCreated.addListener(() => { monitorBackend().catch(console.error) })
browser.windows.onRemoved.addListener(() => handleWindowRemoved().catch(console.error))
browser.alarms.onAlarm.addListener(alarm => {
  if (alarm.name === BACKEND_MONITOR_ALARM) monitorBackend()
})
if (menus?.onClicked) menus.onClicked.addListener(info => { handleMenuClicked(info).catch(console.error) })
if (menus?.onShown) menus.onShown.addListener(info => { handleMenuShown(info).catch(console.error) })

backend.onStatus.add(handleBackendStatus)
const initialization = restoreOptions()
initialization.then(async () => {
  scheduleStartupSynchronization()
  await browser.alarms.create(BACKEND_MONITOR_ALARM, { periodInMinutes: 0.5 })
  await monitorBackend()
  await createMenus()
}).catch(console.error)
