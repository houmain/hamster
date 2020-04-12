
let bookmarkLibrary = undefined

async function initialize () {
  let background = await browser.runtime.getBackgroundPage()
  bookmarkLibrary = background.getBookmarkLibrary()

  localizeControls()
  updateControls()
}

document.addEventListener('DOMContentLoaded', initialize)
