if (this.importScripts) {
    importScripts('../../../resources/js-test.js');
    importScripts('shared.js');
}

var db = null;

const deleteDatabase = () => {
    return new Promise((resolve, reject) => {
        evalAndLog('dbname = "transaction-state-active-after-creation"');
        let deleteRequest = evalAndLog('indexedDB.deleteDatabase(dbname)');
        deleteRequest.onsuccess = () => resolve();
        deleteRequest.onerror = () => reject();
    });
}

const openDatabase = () => {
    return new Promise((resolve, reject) => {
        if (db) {
            debug('database exists');
            resolve();
        } else {
            let openRequest = evalAndLog('indexedDB.open(dbname)');
            openRequest.onupgradeneeded = (event) => {
                db = event.target.result;
                db.createObjectStore('store');
            }
            openRequest.onsuccess =  () => resolve();
            openRequest.onerror = () => reject();
        }
    });
}

const prepareTransaction = async () => {
    await openDatabase();
    evalAndLog('transaction = db.transaction("store", "readwrite")');
}

const putRecord = (key) => {
    return new Promise((resolve, reject) => {
        try {
            putRequest = evalAndLog('transaction.objectStore("store").put("value", "key")');
            putRequest.onsuccess = () => resolve();
            putRequest.onerror = (event) => reject(event);
        } catch(e) {
            reject(e);
        }
    });
}

const sleep = (ms) => {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

const asyncTest = async() => {
    await deleteDatabase();

    await prepareTransaction();
    await putRecord();

    debug('sleep 100ms');
    await sleep(100);

    await prepareTransaction();
    putRecord()
    .then(() => finishJSTest())
    .catch((error) => {
        testFailed('Failed to put record');
        finishJSTest();
    });
}

asyncTest();
