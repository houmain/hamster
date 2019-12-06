'use strict'

const NATIVE_CLIENT_ID = 'pagesowned'
const LIBRARY_TITLE = 'Pages Owned'

;(async function () {
  await WebRecorder.init()
  await BookmarkLibrary.init()
})()
