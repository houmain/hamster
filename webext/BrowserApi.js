'use strict'
/* global chrome */

// Firefox exposes the promise-based WebExtension API as `browser`. Chrome and
// Edge expose the same API as `chrome` in Manifest V3.
if (typeof globalThis.browser === 'undefined') globalThis.browser = globalThis.chrome
