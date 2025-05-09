# Copyright (C) 2017 Igalia S.L.
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
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import logging

pytest_runner = None


def do_delayed_imports():
    global pytest_runner
    import webkitpy.webdriver_tests.pytest_runner as pytest_runner


_log = logging.getLogger(__name__)


class WebDriverSeleniumExecutor(object):

    def __init__(self, port, driver, env):
        self._port = port
        self._driver_name = driver.selenium_name()
        self._env = env
        self._env.update(driver.browser_env())

        self._args = []
        if self._port.get_option('enable_webdriver_bidi'):
            self._args.append('--bidi')

        self._args.extend(['--driver=%s' % self._driver_name, '--driver-binary=%s' % driver.binary_path()])
        browser_path = driver.browser_path()
        if browser_path:
            self._args.extend(['--browser-binary=%s' % browser_path])

        # `--browser_args` must not be followed by other custom options as the space-separated list
        # of arguments to the browser might make pytest lose track of cli parameters while
        # searching for the root config file (e.g. conftest.py).
        # If it fails to find the root config file, it will not add the custom
        # command line parameters, failing to run the suite with `USAGE_ERROR` exit
        # For more info, check the following issues:
        # https://github.com/pytest-dev/pytest/issues/9749
        # https://github.com/python/cpython/issues/66623
        browser_args = driver.browser_args()
        if browser_args:
            self._args.extend(['--browser-args=%s' % ' '.join(browser_args)])

        if pytest_runner is None:
            do_delayed_imports()

    def collect(self, directory):
        return pytest_runner.collect(directory, self._args, self._driver_name)

    def run(self, test, timeout, expectations):
        return pytest_runner.run(test, self._args, timeout, self._env, expectations, self._driver_name)
