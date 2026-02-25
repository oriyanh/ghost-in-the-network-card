import secrets

from flask import Flask, request, make_response



app = Flask(__name__)

@app.route('/cookies')
def index():
    print(f"Rx Cookies={request.cookies}")
    resp = make_response("hello")
    resp.set_cookie('username', 'theusername')
    session_id = secrets.token_urlsafe(6)  # Generates 8 random bytes
    resp.set_cookie('sessionId', session_id)
    print(f"rx_sessionId={request.cookies.get('sessionId')}, tx_sessionId={session_id}")
    print(f"Tx headers=[\n{str(resp.headers)}]\n\n")
    return resp

if __name__ == '__main__':
    app.debug = True
    app.run(host='10.10.1.1', port=80, threaded=True)
