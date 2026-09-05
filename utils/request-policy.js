const DEFAULT_REQUEST_TIMEOUT = 30000;
// The backend can make two DeepSeek attempts. Each WinHTTP attempt has staged
// timeouts totalling up to 50 seconds, so leave room for both plus API overhead.
const MODEL_REQUEST_TIMEOUT = 120000;

module.exports = { DEFAULT_REQUEST_TIMEOUT, MODEL_REQUEST_TIMEOUT };
