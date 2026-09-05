const resultStateAction = (generationStatus, sessionStatus) => {
  if (generationStatus === 'ready') return 'ready';
  if (generationStatus === 'failed') return 'failed';
  if (generationStatus === 'not_started') {
    if (sessionStatus === 'completed') return 'recover-generation';
    if (sessionStatus === 'in_progress') return 'return-to-session';
    return 'return-to-history';
  }
  return 'poll';
};

module.exports = { resultStateAction };
