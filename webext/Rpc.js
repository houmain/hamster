'use strict'
/* global browser */
/* exported HamsterRpc */

const HamsterRpc = {
  async call (method, ...args) {
    const response = await browser.runtime.sendMessage({
      target: 'hamster-background', method, args
    })
    if (!response) throw new Error('The Hamster extension background process did not respond')
    if (!response.ok) throw new Error(response.error || 'Hamster extension request failed')
    return response.value
  }
}
