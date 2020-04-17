'use strict'

let backend = undefined
let bookmarkLibrary = undefined
let filesTree = undefined
let addedUrls = { }

function initializeTree () {
  filesTree = new VanillaTree(document.getElementById('files'), {
    placeholder: 'No leaf is added yet',
    contextmenu: [{
      label: 'Label 1',
      action(id) {
        // someAction
      }
    },{
      label: 'Label 2',
      action(id) {
        // someAction
      }
    }]
  })
}

function splitParentBasePath (url) {
  const maxLevel = 3
  const u = new URL(url)
  if (u.pathname == '/' && !url.endsWith('/')) {
    return {
      parent: undefined,
      base: url,
      level: 0
    }
  }

  let level = u.pathname.match(/\//g).length - 1
  let lastSlash = u.pathname.lastIndexOf('/')
  while (level > maxLevel) {
    lastSlash = u.pathname.lastIndexOf('/', lastSlash - 1)
    --level
  }

  const parentPath = u.pathname.substr(0, lastSlash)
  const baseName = u.pathname.substr(parentPath.length + 1)
  return {
    parent: u.origin + parentPath,
    base: (baseName === '' ? '/' : baseName) + u.search + u.hash,
    level: level
  }
}

function addTreeNode (url, info) {
  if (addedUrls[url]) {
    if (info) {
      filesTree.getLeaf(url).querySelector(".info").textContent = info
    }
    return
  }
  addedUrls[url] = true

  const { parent, base, level } = splitParentBasePath(url)
  if (parent) {
    addTreeNode(parent)
  }

  filesTree.add({
    label: base + '<div class="info info' + level + '">' + (info || '') + '</div>',
    id: url,
    parent: parent,
    opened: true
  })
}

function handleRecordingEvent (event) {
  const { type, status, url } = function () {
    function next() {
      const space = event.indexOf(' ')
      const value = (space >= 0 ? event.substring(0, space) : event)
      event = event.substring(value.length + 1)
      return value
    }
    const type = next()
    if (type === 'DOWNLOAD_FINISHED') {
      return { type: type, status: next(), url: next() }
    }
    return { type: type, status: 0, url: next() }
  }()

  if (url.match(/\//g).length == 2) {
    url += '/'
  }
  addTreeNode(url, type)
}

async function requestListing () {
  const urlParams = new URLSearchParams(window.location.search)
  const bookmarkId = urlParams.get('id')
  if (!bookmarkId) {
    return
  }

  const { path } = await bookmarkLibrary.getBookmarkPath(bookmarkId)
  const response = await backend.getFileListing(path)

  document.getElementById('title').textContent =
    (await Utils.getBookmarkById(bookmarkId)).title

  initializeTree()

  if (response.files) {
    for (let file of response.files) {
      addTreeNode(file.url, Utils.getReadableFileSize(file.compressedSize))
    }
  }

  bookmarkLibrary.addRecordingEventHandler(bookmarkId, handleRecordingEvent)
}

browser.runtime.getBackgroundPage().then(background => {
  backend = background.getBackend()
  bookmarkLibrary = background.getBookmarkLibrary()

  document.addEventListener('DOMContentLoaded', requestListing)
})
