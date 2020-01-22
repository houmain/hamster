
let bookmarkLibrary = undefined

function indentOptions () {
  [].forEach.call(this.options, function(o) {
    o.textContent = o.getAttribute('data-indent') + o.getAttribute('title')
  });
}
function deindentOptions () {
  [].forEach.call(this.options, function(o) {
    o.textContent = o.getAttribute('title')
  });
}

async function updateControls () {
  const bookmark = await bookmarkLibrary.getRecordingBookmarkInActiveTab()
  document.getElementById('bookmark-title').value = bookmark.title

  const options = []
  await bookmarkLibrary.forEachBookmarkFolder(function (folder, level) {
    const indent = '\u2502\xa0\xa0\xa0'.repeat(Math.max(level - 1, 0)) +
      '\u2514\u2500\xa0'.repeat(level ? 1 : 0)
    options.push({
      value: folder.id,
      title: folder.title,
      selected: (folder.id === bookmark.parentId),
      data: { indent: indent }
    })
  })
  Utils.updateSelectOptions('move-bookmark', options)
}

async function moveBookmark () {
  const select = document.getElementById('move-bookmark')
  const option = select.options[select.selectedIndex]
  const bookmark = await bookmarkLibrary.getRecordingBookmarkInActiveTab()
  browser.bookmarks.move(bookmark.id, {
    parentId: option.value
  })
  select.blur()
}

async function updateRefreshMode () {
  const select = document.getElementById('refresh-mode')
  const option = select.options[select.selectedIndex]
  const bookmark = await bookmarkLibrary.getRecordingBookmarkInActiveTab()
  // TODO
  console.log('setting refresh mode of', bookmark.id, 'to', option.value)
}

async function renameBookmark () {
  const bookmark = await bookmarkLibrary.getRecordingBookmarkInActiveTab()
  const title = document.getElementById('bookmark-title').value
  if (bookmark.title !== title) {
    browser.bookmarks.update(bookmark.id, { title: title })
  }
}

async function removeBookmark () {
  const bookmark = await bookmarkLibrary.getRecordingBookmarkInActiveTab()
  browser.bookmarks.remove(bookmark.id)
}

browser.runtime.getBackgroundPage().then(background => {
  bookmarkLibrary = background.getBookmarkLibrary()

  document.getElementById('bookmark-title').onchange = renameBookmark
  document.getElementById('bookmark-title').onkeypress = function (event) {
    if (event.keyCode === 13) {
      event.preventDefault()
      event.target.blur()
    }
  }
  document.getElementById('move-bookmark').onchange = moveBookmark
  document.getElementById('move-bookmark').onfocus = indentOptions
  document.getElementById('move-bookmark').onblur = deindentOptions
  document.getElementById('refresh-mode').onchange = updateRefreshMode
  document.getElementById('remove-bookmark').onclick = removeBookmark
  document.addEventListener('DOMContentLoaded', updateControls)
})
