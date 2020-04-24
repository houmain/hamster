'use strict'

const NATIVE_CLIENT_ID = 'hamster'
const MENU_ROOT_ID = 'menu-root'
const MENU_FILE_LISTING_ID = 'menu-file-listing'
const MENU_OPTIONS_SEPARATOR_ID = 'menu-options-separator'
const MENU_OPTIONS_ID = 'menu-options'

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
    //DEBUG("URL added to history", item.url)
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
  }
  catch (ex) {
  }
  const rootId = await createDefaultBookmarkRoot()
  await Utils.setSetting('bookmark-root-id', rootId)
  return rootId
}

async function restoreOptions () {
  await Utils.setDefaultSetting('default-refresh-mode', 'standard')
  await Utils.setDefaultSetting('allow-lossy-compression', true)
  await Utils.setDefaultSetting('bypass-hosts',
    'www.youtube.com\nwww.vimeo.com')

  const rootId = await initializeBookmarkRoot()
  await bookmarkLibrary.setRootId(rootId)

  const filesystemRoot = await Utils.getSetting('filesystem-root')
  await backend.setFilesystemRoot(filesystemRoot)

  const hostList = await Utils.getSetting('bypass-hosts', '')
  await bookmarkLibrary.setBypassHosts(hostList)
}

function createSuggestions (response) {
  return new Promise(resolve => {
    const suggestions = []
    for (const match of response.matches) {
      suggestions.push({
        content: match.url,
        description: match.snippet,
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
    case "currentTab":
      browser.tabs.update({ url })
      break
    case "newForegroundTab":
      browser.tabs.create({ url })
      break
    case "newBackgroundTab":
      browser.tabs.create({ url, active: false })
      break
  }
}

async function handleFileListingMenuClicked (info) {
  let bookmarkId = info.bookmarkId
  if (Utils.isHttpUrl(info.pageUrl)) {
    const bookmark = await bookmarkLibrary.findBookmarkByUrl(info.pageUrl)
    if (bookmark) {
      bookmarkId = bookmark.id
    }
  }
  if (bookmarkId) {
    const url = browser.runtime.getURL('listing/listing.html?id=' + bookmarkId)
    browser.tabs.create({ url })
  }
}

async function handleOptionsMenuClicked () {
  browser.runtime.openOptionsPage()
}

function createMenus () {
  browser.menus.create({
    id: MENU_ROOT_ID,
    contexts: [ "bookmark", "tab" ],
    title: browser.i18n.getMessage("menu_root")
  })

  browser.menus.create({
    id: MENU_FILE_LISTING_ID,
    parentId: MENU_ROOT_ID,
    contexts: [ "bookmark", "tab" ],
    title: browser.i18n.getMessage("menu_file_listing"),
    onclick: handleFileListingMenuClicked
  })

  browser.menus.create({
    id: MENU_OPTIONS_SEPARATOR_ID,
    parentId: MENU_ROOT_ID,
    type: "separator",
    contexts: [ "bookmark", "tab" ],
  })

  browser.menus.create({
    id: MENU_OPTIONS_ID,
    parentId: MENU_ROOT_ID,
    contexts: [ "bookmark", "tab" ],
    title: browser.i18n.getMessage("menu_options"),
    onclick: handleOptionsMenuClicked
  })

  browser.menus.onShown.addListener(async (info) => {
    let bookmarkId = null
    if (info.contexts.includes("bookmark")) {
      bookmarkId = info.bookmarkId
    }
    if (info.contexts.includes("tab")) {
      if (Utils.isHttpUrl(info.pageUrl)) {
        const bookmark = await bookmarkLibrary.findBookmarkByUrl(info.pageUrl)
        if (bookmark) {
          bookmarkId = bookmark.id
        }
      }
    }
    if (bookmarkId) {
      const isFolder = ((await Utils.getBookmarkById(bookmarkId)).type === "folder")
      const { inLibrary } = await bookmarkLibrary.getBookmarkPath(bookmarkId)
      browser.menus.update(MENU_ROOT_ID, { visible: inLibrary })
      browser.menus.update(MENU_FILE_LISTING_ID, { visible: !isFolder })
      browser.menus.update(MENU_OPTIONS_SEPARATOR_ID, { visible: !isFolder })
    }
    else {
      browser.menus.update(MENU_ROOT_ID, { visible: false })
    }
    browser.menus.refresh()
  })
}

;(async function () {
  await restoreOptions()
  browser.history.onVisited.addListener(handleHistoryChanged)
  browser.omnibox.onInputChanged.addListener(handleOmniBoxInput)
  browser.omnibox.onInputEntered.addListener(handleOmniBoxSelection)

  // TODO: bookmark context menus are only supported by Firefox
  try {
    createMenus()
  }
  catch {
  }
})()
