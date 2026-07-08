#!/bin/bash

# wsrx start script
# Place this file in the same directory as ./wsrx and config.ini.
# Without arguments it behaves like a small watchdog: start wsrx if it is not running.

workingdir="$( cd "$(dirname "$0")" ; pwd -P )"
piddir="${workingdir}/pidfiles"
logdir="${workingdir}/logs"
WSRX="${workingdir}/wsrx"
CONFIG="${workingdir}/config.ini"
PIDFILE="${piddir}/wsrx.pid"
LOGFILE="${logdir}/wsrx.log"

mkdir -p "${piddir}" "${logdir}"

function timestamp {
    date "+%Y-%m-%d %H:%M:%S"
}

function check_binary {
    if [ ! -x "${WSRX}" ]; then
        echo "$(timestamp) [ERROR] I miss executable: ${WSRX}" >&2
        echo "Build it first with: cd ${workingdir} && make" >&2
        exit 1
    fi

    if [ ! -f "${CONFIG}" ]; then
        echo "$(timestamp) [ERROR] I miss config: ${CONFIG}" >&2
        exit 1
    fi
}

function is_running {
    if [ -s "${PIDFILE}" ]; then
        pid="$(cat "${PIDFILE}")"
        if [ -n "${pid}" ] && [ -d "/proc/${pid}" ]; then
            exe="$(readlink "/proc/${pid}/exe" 2>/dev/null || true)"
            if [ "x${exe}" = "x${WSRX}" ]; then
                return 0
            fi
        fi
    fi
    return 1
}

function sanitycheck {
    if [ -s "${PIDFILE}" ]; then
        pid="$(cat "${PIDFILE}")"
        if [ -z "${pid}" ] || [ ! -d "/proc/${pid}" ]; then
            echo "$(timestamp) [WARN] stale pidfile removed: ${PIDFILE}"
            rm -f "${PIDFILE}"
        fi
    fi
}

function start_wsrx {
    check_binary
    sanitycheck

    if is_running; then
        pid="$(cat "${PIDFILE}")"
        echo "$(timestamp) [INFO] wsrx already running pid=${pid}"
        return 0
    fi

    echo "$(timestamp) [INFO] starting wsrx"
    cd "${workingdir}" || exit 1

    # config.ini is intentionally not passed as argument; wsrx reads it from this directory.
    nohup "${WSRX}" >> "${LOGFILE}" 2>&1 &
    pid=$!
    echo "${pid}" > "${PIDFILE}"
    sleep 1

    if is_running; then
        echo "$(timestamp) [INFO] wsrx started pid=${pid} log=${LOGFILE}"
        return 0
    fi

    echo "$(timestamp) [ERROR] wsrx did not stay running. See log: ${LOGFILE}" >&2
    rm -f "${PIDFILE}"
    return 1
}

function stop_wsrx {
    if is_running; then
        pid="$(cat "${PIDFILE}")"
        echo "$(timestamp) [INFO] stopping wsrx pid=${pid}"
        kill "${pid}" 2>/dev/null || true

        for i in $(seq 1 10); do
            if [ ! -d "/proc/${pid}" ]; then
                break
            fi
            sleep 1
        done

        if [ -d "/proc/${pid}" ]; then
            echo "$(timestamp) [WARN] wsrx still running, killing pid=${pid}"
            kill -9 "${pid}" 2>/dev/null || true
        fi
    else
        echo "$(timestamp) [INFO] wsrx is not running"
    fi

    rm -f "${PIDFILE}"
}

function status_wsrx {
    sanitycheck
    if is_running; then
        pid="$(cat "${PIDFILE}")"
        echo "$(timestamp) [INFO] wsrx running pid=${pid}"
        return 0
    fi
    echo "$(timestamp) [INFO] wsrx not running"
    return 1
}

case "x$1" in
    xstart|x)
        start_wsrx
        ;;
    xstop)
        stop_wsrx
        ;;
    xrestart)
        stop_wsrx
        start_wsrx
        ;;
    xstatus)
        status_wsrx
        ;;
    xlog)
        touch "${LOGFILE}"
        tail -f "${LOGFILE}"
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status|log}"
        echo "Without arguments it starts wsrx if it is not already running."
        exit 1
        ;;
esac

exit $?
