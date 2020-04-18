'use strict'

let backend = undefined
let bookmarkLibrary = undefined
let filesTree = undefined
let addedUrls = { }

function initializeTree () {
  const files = document.getElementById('files')
  filesTree = new VanillaTree(files, {
  })

  files.addEventListener('click', (event) => {
    const id = event.target.parentNode.getAttribute('data-vtree-id')
    if (id) {
      filesTree.toggle(id)
    }
  });
}

function splitParentBasePath (url) {
  const maxLevel = 3
  const u = new URL(url)
  if (u.pathname === '/' && !url.endsWith('/') && u.search == 0 && u.hash.length == 0) {
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

function addTreeNode (url, isLeaf, size, status) {
  if (addedUrls[url]) {
    if (size || status) {
      const node = filesTree.getLeaf(url)
      if (size) {
        node.querySelector(".size").textContent = Utils.getReadableFileSize(size)
      }
      if (status) {
        node.querySelector(".status").textContent += status + ', '
      }
    }
    return
  }
  addedUrls[url] = true

  const { parent, base, level } = splitParentBasePath(url)
  if (parent) {
    addTreeNode(parent)
  }

  filesTree.add({
    label: base + '<div class="info info' + level + '"><span class="size">' +
      (size ? Utils.getReadableFileSize(size) : '') + '</span><span class="status"></span></span>',
    id: url,
    parent: parent,
    href: (isLeaf ? url : undefined),
    opened: true
  })
}

function handleRecordingEvent (event) {
  const { type, status, size, url } = function () {
    const p = event.split(' ')
    if (p[0] === 'DOWNLOAD_FINISHED') {
      return { type: p[0], status: p[1], size: p[2], url: p[3] }
    }
    return { type: p[0], url: p[1] }
  }()
  if (type === 'FINISHED') {
    return
  }
  addTreeNode(url, true, size, type)
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
      addTreeNode(file.url, true, file.compressedSize)
    }
  }

  bookmarkLibrary.addRecordingEventHandler(bookmarkId, handleRecordingEvent)
}

browser.runtime.getBackgroundPage().then(background => {
  backend = background.getBackend()
  bookmarkLibrary = background.getBookmarkLibrary()

  document.addEventListener('DOMContentLoaded', requestListing)
})
