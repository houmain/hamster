'use strict'

let bookmarkLibrary = null

async function updatePageAction () {
  let tab = await Utils.getActiveTab()
  let url = bookmarkLibrary.getOriginalUrl(tab.url)
  let icon = (await Utils.findBookmarkByUrl(url) ?
    "icons/recorded.svg" : "icons/icon.svg")
  return browser.pageAction.setIcon({
    tabId: tab.id,
    path: icon
  })
}

async function onPageActionClicked (data) {
  let activeTab = await Utils.getActiveTab()
  let url = bookmarkLibrary.getOriginalUrl(activeTab.url)
  if (await Utils.findBookmarkByUrl(url)) {
    return
  }
  await browser.bookmarks.create({
    parentId: bookmarkLibrary.root.id,
    title: activeTab.title,
    url: url
  })
}

;(async function () {
  let nativeClient = new NativeClient(NATIVE_CLIENT_ID)
  let backend = new Backend(nativeClient)
  bookmarkLibrary = new BookmarkLibrary(backend)
  
  let root = await Utils.findBookmarkFolderByTitle(LIBRARY_TITLE)
  bookmarkLibrary.root = root

  browser.history.onVisited.addListener(item => {
    const url = bookmarkLibrary.getOriginalUrl(item.url)
    if (url !== item.url) {
      browser.history.deleteUrl({ url: item.url })
      browser.history.addUrl({ url: url })
    }
  })

  browser.bookmarks.onCreated.addListener(updatePageAction)
  browser.bookmarks.onChanged.addListener(updatePageAction)
  browser.bookmarks.onRemoved.addListener(updatePageAction)
  browser.bookmarks.onMoved.addListener(updatePageAction)
  browser.tabs.onActivated.addListener(updatePageAction)
  browser.tabs.onUpdated.addListener(updatePageAction)

  browser.pageAction.onClicked.addListener(onPageActionClicked)
})()
