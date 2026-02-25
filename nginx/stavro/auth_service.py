#!/usr/bin/env python3
"""
Minimal authentication service for testing only.

This service receives POST /login with form fields 'username' and 'password'.
If the credentials match the in-file USERS dict it redirects to /success.html.
Otherwise it redirects back to /login.html?error=1.

Security: this is intentionally minimal and for local testing only. Do NOT use
plain-text passwords or this scheme in production. See notes below after the
file for production recommendations.
"""
from flask import Flask, request, redirect, make_response

app = Flask(__name__)

# Simple user store (plain text) — change to your test account.
# In production, use a secure hashed password store (bcrypt/argon2) and TLS.
USERS = {
    "alice": "s3cr3t",
    "bob": "hunter2"
}

@app.route('/login', methods=['POST'])
def login():
    username = request.form.get('username', '')
    password = request.form.get('password', '')

    # Basic validation
    if not username or not password:
        return redirect('/login.html?error=missing')

    expected = USERS.get(username)
    if expected is None or expected != password:
        # invalid credentials
        return redirect('/login.html?error=invalid')

    # On success: set a minimal cookie and redirect to success page.
    # NOTE: This cookie is not signed nor secure — it's only for local testing.
    resp = make_response(redirect('/success.html'))
    resp.set_cookie('test_auth', '1', httponly=True)
    return resp

if __name__ == '__main__':
    # Run on localhost port 5000 — configure nginx to proxy /login here.
    app.run(host='127.0.0.1', port=5000)
