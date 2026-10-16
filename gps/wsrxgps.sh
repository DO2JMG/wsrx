#!/bin/bash

workingdir="$( cd "$(dirname "$0")" ; pwd -P )"
piddir="${workingdir}/pidfiles"
logdir="${workingdir}/logs"

BIN="${workingdir}/wsrxgps"
CONFIG="${workingdir}/../config.ini"   
PIDFILE="${piddir}/wsrxgps.pid"
CLEANER_PIDFILE="${piddir}/wsrxgps-logcleaner.pid"
LOGFILE="${logdir}/wsrxgps.log"

DEVICE="/dev/ttyACM0"
BAUD="9600"

LOG_CLEAR_INTERVAL_SEC=3600

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
    if [ ! -e "${DEVICE}" ]; then
        echo "$(timestamp) [ERROR] I miss GPS device: ${DEVICE}" >&2
        echo "Check with: ls /dev/ttyACM* /dev/ttyUSB*" >&2
        return 1
    fi
    if [ ! -r "${DEVICE}" ]; then
        echo "$(timestamp) [ERROR] no read permission on ${DEVICE}" >&2
        echo "Fix with: sudo usermod -aG dialout \$USER  (then log out and in again)" >&2
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

# --- automatisches Leeren des Logfiles ------------------------------------

function cleaner_running {
    if [ -s "${CLEANER_PIDFILE}" ]; then
        cpid="$(cat "${CLEANER_PIDFILE}")"
        if [ -n "${cpid}" ] && grep -q "_logcleaner" "/proc/${cpid}/cmdline" 2>/dev/null; then
            return 0
        fi
    fi
    return 1
}

function logcleaner_loop {
    local watched_pid="$1"
    local elapsed=0
    while [ -d "/proc/${watched_pid}" ]; do
        sleep 60
        elapsed=$((elapsed + 60))
        if [ "${elapsed}" -ge "${LOG_CLEAR_INTERVAL_SEC}" ]; then
            : > "${LOGFILE}" 2>/dev/null || true
            echo "$(timestamp) [INFO] log cleared automatically (every ${LOG_CLEAR_INTERVAL_SEC} s)" >> "${LOGFILE}"
            elapsed=0
        fi
    done
    rm -f "${CLEANER_PIDFILE}"
}

function start_cleaner {
    [ "${LOG_CLEAR_INTERVAL_SEC}" -gt 0 ] 2>/dev/null || return 0
    stop_cleaner
    nohup "$0" _logcleaner "$1" > /dev/null 2>&1 &
    echo "$!" > "${CLEANER_PIDFILE}"
}

function stop_cleaner {
    if cleaner_running; then
        kill "$(cat "${CLEANER_PIDFILE}")" 2>/dev/null || true
    fi
    rm -f "${CLEANER_PIDFILE}"
}

# --------------------------------------------------------------------------

function start {
    check_binary || return 1
    sanitycheck

    if is_running; then
        pid="$(cat "${PIDFILE}")"
        echo "$(timestamp) [INFO] wsrxgps already running pid=${pid}"
        cleaner_running || start_cleaner "${pid}"
        return 0
    fi

    echo "$(timestamp) [INFO] starting wsrxgps on ${DEVICE} @ ${BAUD} baud"
    cd "${workingdir}" || return 1

    nohup "${BIN}" "${DEVICE}" "${BAUD}" >> "${LOGFILE}" 2>&1 &
    pid=$!
    echo "${pid}" > "${PIDFILE}"
    sleep 1

    if is_running; then
        echo "$(timestamp) [INFO] wsrxgps started pid=${pid} log=${LOGFILE}"
        start_cleaner "${pid}"
        return 0
    fi

    echo "$(timestamp) [ERROR] wsrxgps did not stay running. See log: ${LOGFILE}" >&2
    rm -f "${PIDFILE}"
    return 1
}

function stop {
    stop_cleaner
    if is_running; then
        pid="$(cat "${PIDFILE}")"
        echo "$(timestamp) [INFO] stopping wsrxgps pid=${pid}"
        kill "${pid}" 2>/dev/null || true

        for i in $(seq 1 10); do
            [ ! -d "/proc/${pid}" ] && break
            sleep 1
        done

        if [ -d "/proc/${pid}" ]; then
            echo "$(timestamp) [WARN] wsrxgps still running, killing pid=${pid}"
            kill -9 "${pid}" 2>/dev/null || true
        fi
    else
        echo "$(timestamp) [INFO] wsrxgps is not running"
    fi
    rm -f "${PIDFILE}"
}

function status {
    sanitycheck
    if is_running; then
        pid="$(cat "${PIDFILE}")"
        echo "$(timestamp) [INFO] wsrxgps running pid=${pid}"
        if cleaner_running; then
            echo "$(timestamp) [INFO] log cleaner running pid=$(cat "${CLEANER_PIDFILE}") interval=${LOG_CLEAR_INTERVAL_SEC}s"
        else
            echo "$(timestamp) [INFO] log cleaner not running"
        fi
        return 0
    fi
    echo "$(timestamp) [INFO] wsrxgps not running"
    return 1
}

function log {
    touch "${LOGFILE}"
    tail -f "${LOGFILE}"
}

function clear_logs {
    echo "$(timestamp) [INFO] clearing logs"
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
    x_logcleaner)
        # intern: wird von start im Hintergrund aufgerufen
        logcleaner_loop "$2"
        ;;
    *)
        usage
        exit 1
        ;;
esac