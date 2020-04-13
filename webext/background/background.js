'use strict'

const NATIVE_CLIENT_ID = 'hamster'

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
  const rootId = await initializeBookmarkRoot()
  await bookmarkLibrary.setRootId(rootId)

  const filesystemRoot = await Utils.getSetting('filesystem-root')
  await backend.setFilesystemRoot(filesystemRoot)

  await Utils.setDefaultSetting('default-refresh-mode', 'standard')
  await Utils.setDefaultSetting('allow-lossy-compression', true)
}

function createSuggestions (response) {
  return new Promise(resolve => {
    let suggestions = []
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
  var isHttp = /^https?:/i
  if (!isHttp.test(url)) {
    url = browser.extension.getURL('search/search.html') + '?s=' + url
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

async function handleTabUpdated (tabId, change, tab) {
  if (change.url) {
    const original = bookmarkLibrary.findRecentRecorder(change.url)
    if (original) {
      return browser.tabs.update(tabId, { url: original })
    }
  }
}

;(async function () {
  await restoreOptions()
  browser.history.onVisited.addListener(handleHistoryChanged)
  browser.tabs.onUpdated.addListener(handleTabUpdated)
  browser.omnibox.onInputChanged.addListener(handleOmniBoxInput)
  browser.omnibox.onInputEntered.addListener(handleOmniBoxSelection)
})()
