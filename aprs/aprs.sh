#!/bin/bash

workingdir="$( cd "$(dirname "$0")" ; pwd -P )"
piddir="${workingdir}/pidfiles"
logdir="${workingdir}/logs"

BIN="${workingdir}/wsrxaprs"
CONFIG="${workingdir}/aprs.ini"
PIDFILE="${piddir}/wsrx-aprs-gw.pid"
LOGFILE="${logdir}/wsrx-aprs-gw.log"

mkdir -p "${piddir}" "${logdir}"

function timestamp {
    date "+%Y-%m-%d %H:%M:%S"
}

function check_binary {
    if [ ! -x "${BIN}" ]; then
        echo "$(timestamp) [ERROR] I miss executable: ${BIN}" >&2
        echo "Build it first with: cd ${workingdir} && make" >&2
        return 1
    fi
    if [ ! -f "${CONFIG}" ]; then
        echo "$(timestamp) [ERROR] I miss config: ${CONFIG}" >&2
        return 1
    fi
    return 0
}

function is_running {
    if [ -s "${PIDFILE}" ]; then
        pid="$(cat "${PIDFILE}")"
        if [ -n "${pid}" ] && [ -d "/proc/${pid}" ]; then
            exe="$(readlink "/proc/${pid}/exe" 2>/dev/null || true)"
            if [ "x${exe}" = "x${BIN}" ]; then
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

function start {
    check_binary || return 1
    sanitycheck

    if is_running; then
        pid="$(cat "${PIDFILE}")"
        echo "$(timestamp) [INFO] wsrx-aprs-gw already running pid=${pid}"
        return 0
    fi

    echo "$(timestamp) [INFO] starting wsrx-aprs-gw"
    cd "${workingdir}" || return 1

    nohup "${BIN}" --config "${CONFIG}" >> "${LOGFILE}" 2>&1 &
    pid=$!
    echo "${pid}" > "${PIDFILE}"
    sleep 1

    if is_running; then
        echo "$(timestamp) [INFO] wsrx-aprs-gw started pid=${pid} log=${LOGFILE}"
        return 0
    fi

    echo "$(timestamp) [ERROR] wsrx-aprs-gw did not stay running. See log: ${LOGFILE}" >&2
    rm -f "${PIDFILE}"
    return 1
}

function stop {
    if is_running; then
        pid="$(cat "${PIDFILE}")"
        echo "$(timestamp) [INFO] stopping wsrx-aprs-gw pid=${pid}"
        kill "${pid}" 2>/dev/null || true

        for i in $(seq 1 10); do
            [ ! -d "/proc/${pid}" ] && break
            sleep 1
        done

        if [ -d "/proc/${pid}" ]; then
            echo "$(timestamp) [WARN] wsrx-aprs-gw still running, killing pid=${pid}"
            kill -9 "${pid}" 2>/dev/null || true
        fi
    else
        echo "$(timestamp) [INFO] wsrx-aprs-gw is not running"
    fi
    rm -f "${PIDFILE}"
}

function status {
    sanitycheck
    if is_running; then
        pid="$(cat "${PIDFILE}")"
        echo "$(timestamp) [INFO] wsrx-aprs-gw running pid=${pid}"
        return 0
    fi
    echo "$(timestamp) [INFO] wsrx-aprs-gw not running"
    return 1
}

function log {
    touch "${LOGFILE}"
    tail -f "${LOGFILE}"
}

function clear_logs {
    echo "$(timestamp) [INFO] clearing logs"
    # wsrx-aprs-gw keeps its log file open in append mode, so truncating
    # in place lets it keep writing correctly without needing a restart.
    : > "${LOGFILE}" 2>/dev/null || true
    echo "$(timestamp) [INFO] logs cleared: ${LOGFILE}"
}

function usage {
    echo "Usage: $0 {start|stop|restart|status|log|clearlogs}"
}

case "x$1" in
    xstart|x)
        start
        ;;
    xstop)
        stop
        ;;
    xrestart)
        stop
        start
        ;;
    xstatus)
        status
        ;;
    xlog)
        log
        ;;
    xclearlogs)
        clear_logs
        ;;
    *)
        usage
        exit 1
        ;;
esac
