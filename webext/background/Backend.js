'use strict'

class Backend {

  constructor (nativeClient) {
    this._filesystemRoot = undefined
    this._nativeClient = nativeClient
  }

  async getRequiredVersion () {
    const minorVersionRegex = /\d+\.\d+/;
    const manifest = await browser.runtime.getManifest()
    return minorVersionRegex.exec(manifest.version)[0]
  }

  async getCurrentVersion () {
    try {
      const request = {
        action: 'getStatus',
      }
      const response = await this._nativeClient.sendRequest(request)
      if (response && response.status && response.status.version) {
        const minorVersionRegex = /\d+\.\d+/;
        return minorVersionRegex.exec(response.status.version)[0]
      }
    }
    catch {
    }
  }

  async getVersion () {
    const platformInfo = await browser.runtime.getPlatformInfo()
    const supported = (['linux', 'win'].indexOf(platformInfo.os) >= 0 &&
            platformInfo.arch === 'x86-64')
    const requiredVersion = await this.getRequiredVersion()
    const currentVersion = await this.getCurrentVersion()
    const result = {
      valid: true,
      os: platformInfo.os,
      arch: platformInfo.arch,
      supported,
      requiredVersion,
      currentVersion
    }
    if (!supported || requiredVersion !== currentVersion) {
      result.valid = false
      result.errorMessage =
        (!supported ? "notification_unsupported_system" :
          !currentVersion ? "notification_no_backend" :
          "notification_wrong_backend_version")
    }
    return result
  }

  async checkVersion () {
    return (await this.getVersion()).valid
  }

  async setFilesystemRoot (filesystemRoot) {
    const request = {
      action: 'setLibraryRoot',
      path: (filesystemRoot || this._filesystemRoot)
    }
    const response = await this._nativeClient.sendRequest(request)
    if (response) {
      this._filesystemRoot = response.path
    }
  }

  get filesystemRoot () {
    return this._filesystemRoot
  }

  async startRecording (recorderId, path, url, handleOutput) {
    const refreshMode = await Utils.getSetting('default-refresh-mode')
    const allowLossyCompression = await Utils.getSetting('allow-lossy-compression')
    const request = {
      action: 'startRecording',
      id: recorderId,
      url: url,
      path: path,
      refresh: refreshMode,
      allowLossyCompression: allowLossyCompression,
      deterministic: true,
    }
    await this._nativeClient.sendRequest(request)
    this._pollRecordingOutput(recorderId, handleOutput)
  }

  async stopRecording (recorderId) {
    const request = {
      action: 'stopRecording',
      id: recorderId
    }
    return this._nativeClient.sendRequest(request)
  }

  async getFileSize (path) {
    const request = {
      action: 'getFileSize',
      path: path
    }
    return this._nativeClient.sendRequest(request)
  }

  async moveFile (sourcePath, targetPath) {
    const request = {
      action: 'moveFile',
      from: sourcePath,
      to: targetPath
    }
    return this._nativeClient.sendRequest(request)
  }

  async deleteFile (path, undeleteId) {
    const request = {
      action: 'deleteFile',
      path: path,
      undeleteId: undeleteId
    }
    return this._nativeClient.sendRequest(request)
  }

  async undeleteFile (undeleteId) {
    const request = {
      action: 'undeleteFile',
      undeleteId: undeleteId
    }
    return this._nativeClient.sendRequest(request)
  }

  async browserDirectories (initialPath) {
    const request = {
      action: 'browserDirectories',
      path: initialPath
    }
    return this._nativeClient.sendRequest(request)
  }

  async setHostBlockList (list, append) {
    const request = {
      action: 'setHostsList',
      list: list,
      append: append
    }
    return this._nativeClient.sendRequest(request)
  }

  async updateSearchIndex (path) {
    const request = {
      action: 'updateSearchIndex',
      path: path
    }
    return this._nativeClient.sendRequest(request)
  }

  async executeSearch (query, forSearchPage) {
    // replace space with *
    query = (query + ' ').replace(/\s+/g, '*')

    const request = {
      action: 'executeSearch',
      query: query
    }
    if (forSearchPage) {
      request.highlight = true
      request.snippetSize = 32
      request.maxCount = 20
    }
    return this._nativeClient.sendRequest(request)
  }

  async getFileListing (path) {
    const request = {
      action: 'getFileListing',
      path: path
    }
    return this._nativeClient.sendRequest(request)
  }

  async injectScript (script) {
    const request = {
      action: 'injectScript',
      script: script
    }
    return this._nativeClient.sendRequest(request)
  }

  async setBlockHostsList (hosts) {
    const request = {
      action: 'setBlockHostsList',
      hosts: hosts,
    }
    return this._nativeClient.sendRequest(request)
  }

  async _pollRecordingOutput (recorderId, handleOutput) {
    const request = {
      action: 'getRecordingOutput',
      id: recorderId
    }
    const response = await this._nativeClient.sendRequest(request)
    if (response.events) {
      for (const event of response.events) {
        try {
          await handleOutput(event)
        }
        catch (e) {
          console.error('unhandled exception in output handling:', e.message)
        }
      }
      setTimeout(() => this._pollRecordingOutput(recorderId, handleOutput), 250)
    } else {
      await handleOutput()
    }
  }
}
