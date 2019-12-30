
let bookmarkLibrary = null

function updateControls () {
  const options = []
  bookmarkLibrary.forEachBookmarkFolder(
    function (folder, level) {
      const indent = '\u2502\xa0\xa0\xa0'.repeat(Math.max(level - 1, 0)) + '\u2514\u2574\xa0'.repeat(level ? 1 : 0)
      const option = document.createElement('option')
      option.setAttribute('value', folder.bookmarkId)
      option.appendChild(document.createTextNode(indent + folder.title))
      options.push(option)
    }).then(
    function () {
      const select = document.getElementById('move-bookmark')
      select.innerHTML = ''
      for (const option of options) {
        select.appendChild(option)
      }
    })
}

;(async function () {
  const background = await browser.runtime.getBackgroundPage()
  bookmarkLibrary = background.getBookmarkLibrary()
  document.addEventListener('DOMContentLoaded', updateControls)
})()
