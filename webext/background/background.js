'use strict'

let bookmarkLibrary = null

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

  browser.pageAction.onClicked.addListener(onPageActionClicked)
})()
