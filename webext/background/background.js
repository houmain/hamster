'use strict'

const nativeClient = new NativeClient(NATIVE_CLIENT_ID)
const backend = new Backend(nativeClient)
const bookmarkLibrary = new BookmarkLibrary(getBackend())

function getBackend () {
  return backend
}

function getBookmarkLibrary () {
  return bookmarkLibrary
}

async function updatePageAction () {
  const tab = await Utils.getActiveTab()
  const url = bookmarkLibrary.getOriginalUrl(tab.url)
  const isInLibrary = await bookmarkLibrary.findBookmarkByUrl(url)
  await browser.pageAction.setPopup({
    tabId: tab.id,
    popup: (isInLibrary ? "popup/popup.html" : null)
  })
  await browser.pageAction.setIcon({
    tabId: tab.id,
    path: (isInLibrary ?
      "icons/Save light highlight.svg" :
      "icons/Save light.svg")
  })
}

async function onPageActionClicked (tab) {
  verify(bookmarkLibrary.rootId)
  const url = bookmarkLibrary.getOriginalUrl(tab.url)
  if (await Utils.findBookmarkByUrl(url)) {
    return
  }
  await browser.bookmarks.create({
    parentId: bookmarkLibrary.rootId,
    title: tab.title,
    url: url
  })
  // TODO: remove delay (BookmarkLibrary._handleBookmarkCreated takes a while)
  setTimeout(() => {
    browser.tabs.reload(tab.id)
  }, 100);
}

async function onVisited (item) {
  const url = bookmarkLibrary.getOriginalUrl(item.url)
  if (url !== item.url) {
    await browser.history.deleteUrl({ url: item.url })
    await browser.history.addUrl({ url: url })
  }
  return updatePageAction()
}

async function createDefaultBookmarkRoot () {
  const title = DEFAULT_LIBRARY_TITLE
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
    const rootId = await Utils.getSetting('bookmark_root_id')
    if (rootId) {
      await Utils.getBookmarkById(rootId)
      return rootId
    }
  }
  catch (ex) {
  }
  const rootId = await createDefaultBookmarkRoot()
  await Utils.setSetting('bookmark_root_id', rootId)
  return rootId
}

async function restoreOptions () {
  const rootId = await initializeBookmarkRoot()
  await bookmarkLibrary.setRootId(rootId)

  const filesystemRoot = await Utils.getSetting('filesystem_root')
  await backend.setFilesystemRoot(filesystemRoot)
}

;(async function () {
  await restoreOptions()
  browser.history.onVisited.addListener(onVisited)
  browser.bookmarks.onCreated.addListener(updatePageAction)
  browser.bookmarks.onChanged.addListener(updatePageAction)
  browser.bookmarks.onRemoved.addListener(updatePageAction)
  browser.bookmarks.onMoved.addListener(updatePageAction)
  browser.tabs.onActivated.addListener(updatePageAction)
  browser.pageAction.onClicked.addListener(onPageActionClicked)
})()
