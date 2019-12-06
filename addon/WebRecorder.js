'use strict'

const WebRecorder = {
  async init () {
    NativeClient.onConnected = this.applySettings
  },

  applySettings () {
    const settings = browser.storage.local.get()
    const request = {
      action: 'setLibraryRoot',
      path: settings.libraryRoot
    }
    NativeClient.sendRequest(request)
  },

  async pollRecordingOutput (recorderId, handleOutput) {
    const request = {
      action: 'getRecordingOutput',
      id: recorderId
    }
    const response = await NativeClient.sendRequest(request)
    if (response.events) {
      for (const event of response.events) {
        await handleOutput(event)
      }
      setTimeout(() => this.pollRecordingOutput(recorderId, handleOutput), 500)
    }
    else {
      await handleOutput()
    }
  },

  async startRecording (recorderId, path, filename, url, handleOutput) {
    const request = {
      action: 'startRecording',
      id: recorderId,
      filename: filename,
      url: url,
      path: path
    }
    await NativeClient.sendRequest(request)
    this.pollRecordingOutput(recorderId, handleOutput)
  },

  async stopRecording (recorderId) {
    const request = {
      action: 'stopRecording',
      id: recorderId
    }
    return NativeClient.sendRequest(request)
  },

  async moveFile (sourcePath, targetPath) {
    const request = {
      action: 'moveFile',
      from: sourcePath,
      to: targetPath
    }
    return NativeClient.sendRequest(request)
  },

  async deleteFile (path, undeleteId) {
    const request = {
      action: 'deleteFile',
      path: path,
      undeleteId: undeleteId
    }
    return NativeClient.sendRequest(request)
  },

  async undeleteFile (undeleteId) {
    const request = {
      action: 'undeleteFile',
      undeleteId: undeleteId
    }
    return NativeClient.sendRequest(request)
  }
}