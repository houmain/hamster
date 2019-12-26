'use strict'

let bookmarkLibrary = null

;(async function () {
  let nativeClient = new NativeClient()
  let backend = new Backend(nativeClient)
  bookmarkLibrary = new BookmarkLibrary(backend)
  
  let root = await Utils.findLibraryRoot()
  bookmarkLibrary.setRoot(root)

  browser.history.onVisited.addListener((item) => {
    if (item.url.startsWith('http://127.0.0.1')) {
      browser.history.deleteUrl({ url: item.url })
    }
  })  
})()
