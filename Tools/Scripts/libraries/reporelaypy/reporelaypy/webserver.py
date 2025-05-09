# Copyright (C) 2021 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import json
import os
import time

from flask_cors import CORS

autoinstall_path = os.environ.get('AUTOINSTALL_PATH')
if autoinstall_path:
    from webkitcorepy import AutoInstall
    AutoInstall.set_directory(autoinstall_path)

from flask import Flask, current_app, json as fjson, render_template
from reporelaypy import Checkout, CheckoutRoute, Redirector, HookReceiver
from webkitflaskpy import Database

HEARTBEAT_LIMIT = 300

app = Flask(__name__)
CORS(app)

database = Database()
checkout = Checkout.from_json(os.environ.get('CHECKOUT', '{}'))
checkout_routes = CheckoutRoute(
    checkout, redirectors=Redirector.from_json(os.environ.get('REDIRECTORS', '[]')),
    import_name=__name__, database=database,
    blocked_user_agents=json.loads(os.environ.get('BLOCKED_USER_AGENTS', '[]')),
)

hook_args = json.loads(os.environ.get('HOOKS', '{}'))
if hook_args.get('enabled', False):
    hook_routes = HookReceiver(
        import_name=__name__, database=database,
        debug=hook_args.get('debug', False), secret=os.environ.get(HookReceiver.SECRET_ENV),
    )
else:
    hook_routes = None


@app.route('/__health')
def health():
    if not checkout.repository:
        return current_app.response_class(
            fjson.dumps(dict(status='cloning'), indent=4),
            mimetype='application/json',
        )

    if not checkout_routes.commit(ref='main'):
        return current_app.response_class(
            fjson.dumps(dict(
                status='broken',
                why="Cannot construct commit from 'main'",
            ), indent=4),
            mimetype='application/json',
            status=500,
        )

    heartbeat = '/tmp/heartbeat.txt'
    if os.path.isfile(heartbeat):
        with open(heartbeat, 'r') as heartbeat:
            heartbeat_time = heartbeat.read().strip()
        if not heartbeat_time.isnumeric():
            return current_app.response_class(
                fjson.dumps(dict(
                    status='broken',
                    why='Invalid heartbeat file',
                ), indent=4),
                mimetype='application/json',
                status=500,
            )

        time_since_heartbeat = time.time() - int(heartbeat_time)
        if HEARTBEAT_LIMIT < time_since_heartbeat:
            return current_app.response_class(
                fjson.dumps(dict(
                    status='broken',
                    why=f'Heartbeat file is {time_since_heartbeat} seconds old',
                ), indent=4),
                mimetype='application/json',
                status=500,
            )

    return current_app.response_class(
        fjson.dumps(dict(status='ready'), indent=4),
        mimetype='application/json',
    )


@app.route('/')
def compare():
    return render_template('compare.html')


# Crawling a reporelay service is always a bad idea
@app.route('/robots.txt')
def robots_txt():
    return '''User-agent: *
Disallow: /
'''


app.register_blueprint(checkout_routes)
if hook_routes:
    app.register_blueprint(hook_routes)


if __name__ == '__main__':
    port = int(os.environ.get('PORT', 5000))
    app.run(host='0.0.0.0', port=port)
