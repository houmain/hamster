'use strict'

class Utils {
  static async getBookmarksRoot () {
    let roots = await browser.bookmarks.get('root________')
    if (browser.runtime.lastError) {
      roots = await browser.bookmarks.get('0')
    }
    return roots[0]
  }

  static async getBookmark (bookmarkId) {
    return (await browser.bookmarks.get(bookmarkId))[0];
  }

  static async findLibraryRoot () {
    const root = await this.getBookmarksRoot()
    const bases = await browser.bookmarks.getChildren(root.id)
    for (const base of bases) {
      const children = await browser.bookmarks.getChildren(base.id)
      const child = children.find(c => c.title === LIBRARY_TITLE)
      if (child) {
        return child
      }
    }
  }
}
