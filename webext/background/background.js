'use strict'

let backend = null
let bookmarkLibrary = null

function getBackend () {
  return backend
}

function getBookmarkLibrary () {
  return bookmarkLibrary
}

async function updatePageAction (tabId, event) {
  let tab = await Utils.getActiveTab()
  let url = bookmarkLibrary.getOriginalUrl(tab.url)
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
  let url = bookmarkLibrary.getOriginalUrl(tab.url)
  if (await Utils.findBookmarkByUrl(url)) {
    return
  }
  return browser.bookmarks.create({
    parentId: bookmarkLibrary.root.id,
    title: tab.title,
    url: url
  })
  //browser.tabs.reload(activeTab.id)
}

async function onVisited (item) {
  const url = bookmarkLibrary.getOriginalUrl(item.url)
  if (url !== item.url) {
    await browser.history.deleteUrl({ url: item.url })
    await browser.history.addUrl({ url: url })
  }
  return updatePageAction()
}

;(async function () {
  let nativeClient = new NativeClient(NATIVE_CLIENT_ID)
  backend = new Backend(nativeClient)
  bookmarkLibrary = new BookmarkLibrary(backend)

  let root = await Utils.findBookmarkFolderByTitle(LIBRARY_TITLE)
  bookmarkLibrary.root = root

  browser.history.onVisited.addListener(onVisited)
  browser.bookmarks.onCreated.addListener(updatePageAction)
  browser.bookmarks.onChanged.addListener(updatePageAction)
  browser.bookmarks.onRemoved.addListener(updatePageAction)
  browser.bookmarks.onMoved.addListener(updatePageAction)
  browser.tabs.onActivated.addListener(updatePageAction)
  browser.pageAction.onClicked.addListener(onPageActionClicked)
})()
