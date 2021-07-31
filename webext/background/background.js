'use strict'
/* global Utils, DEBUG, NativeClient, Backend, BookmarkLibrary */

const NATIVE_CLIENT_ID = 'hamster'
const MENU_ROOT_ID = 'menu-root'
const MENU_FILE_LISTING_ID = 'menu-file-listing'
const MENU_COPY_URL_ID = 'menu-copy-url'
const MENU_OPEN_ORIGINAL_ID = 'menu-open-original'
const MENU_OPTIONS_SEPARATOR_ID = 'menu-options-separator'
const MENU_OPTIONS_ID = 'menu-options'
const DEFAULT_BYPASS_HOSTS_LIST =
`youtube.com
vimeo.com
digiteka.net
`

const nativeClient = new NativeClient(NATIVE_CLIENT_ID)
const backend = new Backend(nativeClient)
const bookmarkLibrary = new BookmarkLibrary(getBackend())

function getBackend () {
  return backend
}

function getBookmarkLibrary () {
  return bookmarkLibrary
}

async function handleHistoryChanged (item) {
  const url = bookmarkLibrary.getOriginalUrl(item.url)
  if (url !== item.url) {
    await browser.history.deleteUrl({ url: item.url })
    await browser.history.addUrl({ url: url })
  } else {
    DEBUG('URL added to history', item.url)
  }
}

async function createDefaultBookmarkRoot () {
  const title = browser.i18n.getMessage('default_bookmark_root_title')
  const bases = await Utils.getBookmarkBaseFolders()
  for (const base of bases) {
    const children = await browser.bookmarks.getChildren(base.id)
    const child = children.find(c => c.type === 'folder' && c.title === title)
    if (child) {
      return child.id
    }
  }
  const root = await browser.bookmarks.create({
    parentId: bases[0].id,
    title: title
  })
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

    const hostList = await Utils.getSetting('bypass-hosts', '')
    await bookmarkLibrary.setBypassHosts(hostList)

    if (await backend.checkVersion()) {
      const filesystemRoot = await Utils.getSetting('filesystem-root')
      await backend.setFilesystemRoot(filesystemRoot)

      const rootId = await initializeBookmarkRoot()
      await bookmarkLibrary.setRootId(rootId)
    }
  } catch (ex) {
    DEBUG('restoring options failed: ', ex)
  }
}

function createSuggestions (response) {
  return new Promise(resolve => {
    const suggestions = []
    for (const match of response.matches) {
      suggestions.push({
        content: match.url,
        description: match.snippet
      })
    }
    return resolve(suggestions)
  })
}

function handleOmniBoxInput (text, addSuggestions) {
  browser.omnibox.setDefaultSuggestion({
    description: browser.i18n.getMessage('omnibox_default_search')
  })
  backend.executeSearch(text)
    .then(createSuggestions).then(addSuggestions)
}

function handleOmniBoxSelection (url, disposition) {
  const isHttp = /^https?:/i
  if (!isHttp.test(url)) {
    url = browser.runtime.getURL('search/search.html') + '?s=' + url
  }
  switch (disposition) {
    case 'currentTab':
      browser.tabs.update({ url })
      break
    case 'newForegroundTab':
      browser.tabs.create({ url })
      break
    case 'newBackgroundTab':
      browser.tabs.create({ url, active: false })
      break
  }
}

async function _getContextBookmarkId (info) {
  if (Utils.isHttpUrl(info.pageUrl)) {
    return bookmarkLibrary.findBookmarkByUrl(info.pageUrl)
  }
  return Utils.getBookmarkById(info.bookmarkId)
}

async function handleFileListingMenuClicked (info) {
  const bookmark = await _getContextBookmarkId(info)
  const url = browser.runtime.getURL('listing/listing.html?id=' + bookmark.id)
  browser.tabs.create({ url })
}

async function handleCopyUrlMenuClicked (info) {
  const bookmark = await _getContextBookmarkId(info)
  navigator.clipboard.writeText(
    bookmarkLibrary.getOriginalUrl(info.pageUrl || bookmark.url))
}

async function handleOpenOriginalMenuClicked (info) {
  const bookmark = await _getContextBookmarkId(info)
  if (bookmarkLibrary.temporarilyBypassBookmark(bookmark.id)) {
    browser.tabs.create({
      url: bookmarkLibrary.getOriginalUrl(info.pageUrl || bookmark.url)
    })
  }
}

async function handleOptionsMenuClicked () {
  browser.runtime.openOptionsPage()
}

function createMenus () {
  browser.menus.create({
    id: MENU_ROOT_ID,
    contexts: ['bookmark', 'tab'],
    title: browser.i18n.getMessage('menu_root')
  })

  browser.menus.create({
    id: MENU_OPEN_ORIGINAL_ID,
    parentId: MENU_ROOT_ID,
    contexts: ['bookmark', 'tab'],
    title: browser.i18n.getMessage('menu_open_original'),
    onclick: handleOpenOriginalMenuClicked
  })

  browser.menus.create({
    id: MENU_COPY_URL_ID,
    parentId: MENU_ROOT_ID,
    contexts: ['bookmark', 'tab'],
    title: browser.i18n.getMessage('menu_copy_url'),
    onclick: handleCopyUrlMenuClicked
  })

  browser.menus.create({
    id: MENU_FILE_LISTING_ID,
    parentId: MENU_ROOT_ID,
    contexts: ['bookmark', 'tab'],
    title: browser.i18n.getMessage('menu_file_listing'),
    onclick: handleFileListingMenuClicked
  })

  browser.menus.create({
    id: MENU_OPTIONS_SEPARATOR_ID,
    parentId: MENU_ROOT_ID,
    type: 'separator',
    contexts: ['bookmark', 'tab']
  })

  browser.menus.create({
    id: MENU_OPTIONS_ID,
    parentId: MENU_ROOT_ID,
    contexts: ['bookmark', 'tab'],
    title: browser.i18n.getMessage('menu_options'),
    onclick: handleOptionsMenuClicked
  })
  browser.menus.refresh()

  browser.menus.onShown.addListener(async (info) => {
    let bookmarkId = null
    if (info.contexts.includes('bookmark')) {
      bookmarkId = info.bookmarkId
    }
    if (info.contexts.includes('tab')) {
      if (Utils.isHttpUrl(info.pageUrl)) {
        const bookmark = await bookmarkLibrary.findBookmarkByUrl(info.pageUrl)
        if (bookmark) {
          bookmarkId = bookmark.id
        }
      }
    }
    if (bookmarkId) {
      const isFolder = ((await Utils.getBookmarkById(bookmarkId)).type === 'folder')
      const { inLibrary } = await bookmarkLibrary.getBookmarkPath(bookmarkId)
      browser.menus.update(MENU_ROOT_ID, { visible: inLibrary })
      browser.menus.update(MENU_OPEN_ORIGINAL_ID, { visible: !isFolder })
      browser.menus.update(MENU_COPY_URL_ID, { visible: !isFolder })
      browser.menus.update(MENU_FILE_LISTING_ID, { visible: !isFolder })
      browser.menus.update(MENU_OPTIONS_SEPARATOR_ID, { visible: !isFolder })
    } else {
      browser.menus.update(MENU_ROOT_ID, { visible: false })
    }
    browser.menus.refresh()
  })
}

;(async function () {
  browser.history.onVisited.addListener(handleHistoryChanged)
  browser.omnibox.onInputChanged.addListener(handleOmniBoxInput)
  browser.omnibox.onInputEntered.addListener(handleOmniBoxSelection)
  browser.notifications.onClicked.addListener(handleOptionsMenuClicked)

  const errorMessage = (await backend.getVersion()).errorMessage
  if (errorMessage) {
    browser.notifications.create({
      type: 'basic',
      iconUrl: browser.extension.getURL('icons/icon.svg'),
      title: browser.i18n.getMessage('notification_no_backend_title'),
      message: browser.i18n.getMessage(errorMessage)
    })
  }

  await restoreOptions()

  // TODO: bookmark context menus are only supported by Firefox
  try {
    createMenus()
  } catch {
  }
})()
