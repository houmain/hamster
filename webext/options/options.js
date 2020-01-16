
let backend = null
let bookmarkLibrary = null

async function findLibraryRootByTitle (title) {
  verify(title)
  const bases = await Utils.getBookmarkBaseFolders()
  for (const base of bases) {
    const children = await browser.bookmarks.getChildren(base.id)
    const child = children.find(c => c.type === 'folder' && c.title === title)
    if (child) {
      return child
    }
  }
  return browser.bookmarks.create({
    parentId: bases[0].id,
    title: title
  });
}

async function updateControls () {
  const options = []
  for (const folder of await Utils.getBookmarkBaseFolders()) {
    options.push({
      value: folder.id,
      title: folder.title
    })
  }
  Utils.updateSelectOptions('bookmark-root-parent', options)
}

async function restoreOptions () {
  await updateControls()

  try {
    const bookmarkRootId = await Utils.getSetting('bookmark_root_id')
    if (bookmarkRootId) {
      bookmarkLibrary.root = await Utils.getBookmarkById(bookmarkRootId)
    }
  }
  catch (ex) {
  }
  if (!bookmarkLibrary.root) {
    bookmarkLibrary.root = await findLibraryRootByTitle(DEFAULT_LIBRARY_TITLE)
    await Utils.setSetting('bookmark_root_id', bookmarkLibrary.root.id)
  }

  const parent = document.getElementById('bookmark-root-parent')
  parent.value = bookmarkLibrary.root.parentId

  const title = document.getElementById('bookmark-root-title')
  title.value = bookmarkLibrary.root.title

  const filesystemRoot = await Utils.getSetting('filesystem_root')
  await backend.setFilesystemRoot(filesystemRoot)
  const root = document.getElementById('filesystem-root')
  root.value = backend.filesystemRoot
}

async function handleBrowseClicked () {
  const result = await backend.browserDirectories(backend.filesystemRoot)
  if (result.path) {
    await Utils.setSetting('filesystem_root', result.path)
    return restoreOptions()
  }
}

browser.runtime.getBackgroundPage().then(background => {
  backend = background.getBackend()
  bookmarkLibrary = background.getBookmarkLibrary()

  const browse = document.getElementById('filesystem-root-browse')
  browse.onclick = handleBrowseClicked

  document.addEventListener('DOMContentLoaded', restoreOptions)
})
