'use strict'

let bookmarkLibrary = null

;(async function () {
  let nativeClient = new NativeClient()
  let backend = new Backend(nativeClient)
  bookmarkLibrary = new BookmarkLibrary(backend)
  
  let root = await Utils.findBookmarkFolder(LIBRARY_TITLE)
  bookmarkLibrary.setRoot(root)

  browser.history.onVisited.addListener(item => {
    let originalUrl = bookmarkLibrary.getOriginalUrl(item.url)
    if (originalUrl) {
      browser.history.deleteUrl({ url: item.url })
      browser.history.addUrl({ url: originalUrl })
    }
  })  
})()
