
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

async function getRecordingInfo() {
  const tab = await Utils.getActiveTab()
  return bookmarkLibrary.getRecordingInfo(tab)
}

async function getRecordingBookmark() {
  const info = await getRecordingInfo()
  return Utils.getBookmarkById(info.bookmarkId)
}

function humanFileSize(bytes) {
  const units = ['bytes', 'KiB','MiB','GiB']
  for (let u = 0;; ++u, bytes /= 1024) {
    if (bytes < 1024 || u == units.length - 1) {
      return bytes.toFixed(1) + ' ' + units[u]
    }
  }
}

async function updateControls () {
  const bookmark = await getRecordingBookmark()
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

  const info = await getRecordingInfo()
  const status = document.getElementById('bookmark-status')
  status.innerText = 'Filesize: ' + humanFileSize(info.fileSize ? info.fileSize : 0)
}

async function moveBookmark () {
  const select = document.getElementById('move-bookmark')
  const option = select.options[select.selectedIndex]
  const bookmark = await getRecordingBookmark()
  browser.bookmarks.move(bookmark.id, {
    parentId: option.value
  })
  select.blur()
}

async function updateRefreshMode () {
  const select = document.getElementById('refresh-mode')
  const option = select.options[select.selectedIndex]
  const bookmark = await getRecordingBookmark()
  // TODO
  console.log('setting refresh mode of', bookmark.id, 'to', option.value)
}

async function renameBookmark () {
  const bookmark = await getRecordingBookmark()
  const title = document.getElementById('bookmark-title').value
  if (bookmark.title !== title) {
    browser.bookmarks.update(bookmark.id, { title: title })
  }
}

async function removeBookmark () {
  const bookmark = await getRecordingBookmark()
  bookmarkLibrary.removeBookmark(bookmark.id)
  window.close()
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
