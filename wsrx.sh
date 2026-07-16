#!/bin/bash

workingdir="$( cd "$(dirname "$0")" ; pwd -P )"
piddir="${workingdir}/pidfiles"
logdir="${workingdir}/logs"

WSRX="${workingdir}/wsrx"
CONFIG="${workingdir}/config.ini"
WSRX_PIDFILE="${piddir}/wsrx.pid"
WSRX_LOGFILE="${logdir}/wsrx.log"

WEB="${workingdir}/wsrx-web"
WEB_PIDFILE="${piddir}/wsrx-web.pid"
WEB_LOGFILE="${logdir}/wsrx-web.log"
BIND="${WSRX_WEB_BIND:-0.0.0.0}"
PORT="${WSRX_WEB_PORT:-8073}"

mkdir -p "${piddir}" "${logdir}"

function timestamp {
    date "+%Y-%m-%d %H:%M:%S"
}

# ---------------------------------------------------------------- wsrx

function check_binary_wsrx {
    if [ ! -x "${WSRX}" ]; then
        echo "$(timestamp) [ERROR] I miss executable: ${WSRX}" >&2
        echo "Build it first with: cd ${workingdir} && make" >&2
        return 1
    fi
    if [ ! -f "${CONFIG}" ]; then
        echo "$(timestamp) [ERROR] I miss config: ${CONFIG}" >&2
        return 1
    fi
    return 0
}

function is_running_wsrx {
    if [ -s "${WSRX_PIDFILE}" ]; then
        pid="$(cat "${WSRX_PIDFILE}")"
        if [ -n "${pid}" ] && [ -d "/proc/${pid}" ]; then
            exe="$(readlink "/proc/${pid}/exe" 2>/dev/null || true)"
            if [ "x${exe}" = "x${WSRX}" ]; then
                return 0
            fi
        fi
    fi
    return 1
}

function sanitycheck_wsrx {
    if [ -s "${WSRX_PIDFILE}" ]; then
        pid="$(cat "${WSRX_PIDFILE}")"
        if [ -z "${pid}" ] || [ ! -d "/proc/${pid}" ]; then
            echo "$(timestamp) [WARN] stale pidfile removed: ${WSRX_PIDFILE}"
            rm -f "${WSRX_PIDFILE}"
        fi
    fi
}

function start_wsrx {
    check_binary_wsrx || return 1
    sanitycheck_wsrx

    if is_running_wsrx; then
        pid="$(cat "${WSRX_PIDFILE}")"
        echo "$(timestamp) [INFO] wsrx already running pid=${pid}"
        return 0
    fi

    echo "$(timestamp) [INFO] starting wsrx"
    cd "${workingdir}" || return 1

    # config.ini is intentionally not passed as argument; wsrx reads it from this directory.
    nohup "${WSRX}" >> "${WSRX_LOGFILE}" 2>&1 &
    pid=$!
    echo "${pid}" > "${WSRX_PIDFILE}"
    sleep 1

    if is_running_wsrx; then
        echo "$(timestamp) [INFO] wsrx started pid=${pid} log=${WSRX_LOGFILE}"
        return 0
    fi

    echo "$(timestamp) [ERROR] wsrx did not stay running. See log: ${WSRX_LOGFILE}" >&2
    rm -f "${WSRX_PIDFILE}"
    return 1
}

function stop_wsrx {
    if is_running_wsrx; then
        pid="$(cat "${WSRX_PIDFILE}")"
        echo "$(timestamp) [INFO] stopping wsrx pid=${pid}"
        kill "${pid}" 2>/dev/null || true

        for i in $(seq 1 10); do
            [ ! -d "/proc/${pid}" ] && break
            sleep 1
        done

        if [ -d "/proc/${pid}" ]; then
            echo "$(timestamp) [WARN] wsrx still running, killing pid=${pid}"
            kill -9 "${pid}" 2>/dev/null || true
        fi
    else
        echo "$(timestamp) [INFO] wsrx is not running"
    fi
    rm -f "${WSRX_PIDFILE}"
}

function status_wsrx {
    sanitycheck_wsrx
    if is_running_wsrx; then
        pid="$(cat "${WSRX_PIDFILE}")"
        echo "$(timestamp) [INFO] wsrx running pid=${pid}"
        return 0
    fi
    echo "$(timestamp) [INFO] wsrx not running"
    return 1
}

function log_wsrx {
    touch "${WSRX_LOGFILE}"
    tail -f "${WSRX_LOGFILE}"
}

# ---------------------------------------------------------------- wsrx-web

function print_urls_web {
    echo "$(timestamp) [INFO] bind=${BIND} port=${PORT}"
    echo "$(timestamp) [INFO] local:   http://127.0.0.1:${PORT}/"
    ips="$(hostname -I 2>/dev/null || true)"
    for ip in ${ips}; do
        case "${ip}" in
            127.*|::1) ;;
            *:*) ;;
            *) echo "$(timestamp) [INFO] network: http://${ip}:${PORT}/" ;;
        esac
    done
}

function print_listen_web {
    if command -v ss >/dev/null 2>&1; then
        echo "$(timestamp) [INFO] listening sockets for :${PORT}:"
        ss -ltnp 2>/dev/null | grep ":${PORT} " || true
    elif command -v netstat >/dev/null 2>&1; then
        echo "$(timestamp) [INFO] listening sockets for :${PORT}:"
        netstat -ltnp 2>/dev/null | grep ":${PORT} " || true
    fi
}

function check_binary_web {
    if [ ! -x "${WEB}" ]; then
        echo "$(timestamp) [ERROR] missing executable: ${WEB}" >&2
        echo "Build it first with: cd ${workingdir} && make" >&2
        return 1
    fi
    return 0
}

function pid_is_wsrx_web {
    pid="$1"
    [ -n "${pid}" ] || return 1
    [ -d "/proc/${pid}" ] || return 1
    exe="$(readlink "/proc/${pid}/exe" 2>/dev/null || true)"
    if [ "x${exe}" = "x${WEB}" ]; then
        return 0
    fi
    cmd="$(tr '\0' ' ' < "/proc/${pid}/cmdline" 2>/dev/null || true)"
    case "${cmd}" in
        *"${WEB}"*|*"wsrx-web"*) return 0 ;;
    esac
    return 1
}

function is_running_web {
    if [ -s "${WEB_PIDFILE}" ]; then
        pid="$(cat "${WEB_PIDFILE}")"
        if pid_is_wsrx_web "${pid}"; then
            return 0
        fi
    fi
    return 1
}

function sanitycheck_web {
    if [ -s "${WEB_PIDFILE}" ]; then
        pid="$(cat "${WEB_PIDFILE}")"
        if ! pid_is_wsrx_web "${pid}"; then
            echo "$(timestamp) [WARN] stale pidfile removed: ${WEB_PIDFILE}"
            rm -f "${WEB_PIDFILE}"
        fi
    fi
}

function port_pids_web {
    if command -v ss >/dev/null 2>&1; then
        ss -ltnp 2>/dev/null | awk -v p=":${PORT}" '$4 ~ p {print $0}' | sed -n 's/.*pid=\([0-9][0-9]*\).*/\1/p' | sort -u
    elif command -v lsof >/dev/null 2>&1; then
        lsof -tiTCP:"${PORT}" -sTCP:LISTEN 2>/dev/null | sort -u
    fi
}

function stop_pid_web {
    pid="$1"
    [ -n "${pid}" ] || return 0
    [ -d "/proc/${pid}" ] || return 0
    echo "$(timestamp) [INFO] stopping wsrx-web pid=${pid}"
    kill "${pid}" 2>/dev/null || true
    for i in $(seq 1 5); do
        [ ! -d "/proc/${pid}" ] && return 0
        sleep 1
    done
    if [ -d "/proc/${pid}" ]; then
        echo "$(timestamp) [WARN] wsrx-web still running, killing pid=${pid}"
        kill -9 "${pid}" 2>/dev/null || true
    fi
}

function cleanup_old_web_processes {
    # Stop stale wsrx-web processes that still hold the configured port.
    for pid in $(port_pids_web); do
        if pid_is_wsrx_web "${pid}"; then
            stop_pid_web "${pid}"
        else
            echo "$(timestamp) [ERROR] port ${PORT} is used by another process pid=${pid}" >&2
            print_listen_web >&2
            return 1
        fi
    done
    return 0
}

function start_web {
    check_binary_web || return 1
    sanitycheck_web

    if is_running_web; then
        pid="$(cat "${WEB_PIDFILE}")"
        echo "$(timestamp) [INFO] wsrx-web already running pid=${pid}"
        print_urls_web
        return 0
    fi

    cleanup_old_web_processes || return 1

    echo "$(timestamp) [INFO] starting wsrx-web bind=${BIND} port=${PORT}"
    cd "${workingdir}" || return 1
    nohup "${WEB}" -bind "${BIND}" -port "${PORT}" -dir "${workingdir}" >> "${WEB_LOGFILE}" 2>&1 &
    pid=$!
    echo "${pid}" > "${WEB_PIDFILE}"
    sleep 1

    if is_running_web; then
        echo "$(timestamp) [INFO] wsrx-web started pid=${pid} log=${WEB_LOGFILE}"
        print_urls_web
        print_listen_web
        return 0
    fi

    if grep -q "Address already in use" "${WEB_LOGFILE}" 2>/dev/null; then
        echo "$(timestamp) [ERROR] port ${PORT} is already in use" >&2
        print_listen_web >&2
    else
        echo "$(timestamp) [ERROR] wsrx-web did not stay running. See log: ${WEB_LOGFILE}" >&2
    fi
    rm -f "${WEB_PIDFILE}"
    return 1
}

function stop_web {
    sanitycheck_web
    if is_running_web; then
        pid="$(cat "${WEB_PIDFILE}")"
        stop_pid_web "${pid}"
    else
        echo "$(timestamp) [INFO] wsrx-web is not running"
    fi
    rm -f "${WEB_PIDFILE}"

    # Also remove stale wsrx-web instances on the same port.
    for pid in $(port_pids_web); do
        if pid_is_wsrx_web "${pid}"; then
            stop_pid_web "${pid}"
        fi
    done
}

function status_web {
    sanitycheck_web
    if is_running_web; then
        pid="$(cat "${WEB_PIDFILE}")"
        echo "$(timestamp) [INFO] wsrx-web running pid=${pid}"
        print_urls_web
        print_listen_web
        return 0
    fi
    echo "$(timestamp) [INFO] wsrx-web not running"
    print_listen_web
    return 1
}

function log_web {
    touch "${WEB_LOGFILE}"
    tail -f "${WEB_LOGFILE}"
}

# ---------------------------------------------------------------- dispatch

function usage {
    echo "Usage: $0 {start|stop|restart|status|log} [wsrx|web]"
    echo "Without a target, the command applies to both wsrx and wsrx-web."
    echo "Environment (wsrx-web only): WSRX_WEB_BIND=0.0.0.0 WSRX_WEB_PORT=8073"
}

target="$2"
case "x${target}" in
    x|xboth) targets="wsrx web" ;;
    xwsrx)   targets="wsrx" ;;
    xweb)    targets="web" ;;
    *)
        echo "Unknown target: ${target}" >&2
        usage
        exit 1
        ;;
esac

rc=0
case "x$1" in
    xstart|x)
        for t in ${targets}; do
            if [ "${t}" = "wsrx" ]; then start_wsrx || rc=1; else start_web || rc=1; fi
        done
        ;;
    xstop)
        for t in ${targets}; do
            if [ "${t}" = "wsrx" ]; then stop_wsrx || rc=1; else stop_web || rc=1; fi
        done
        ;;
    xrestart)
        for t in ${targets}; do
            if [ "${t}" = "wsrx" ]; then stop_wsrx; start_wsrx || rc=1; else stop_web; start_web || rc=1; fi
        done
        ;;
    xstatus)
        for t in ${targets}; do
            if [ "${t}" = "wsrx" ]; then status_wsrx || rc=1; else status_web || rc=1; fi
        done
        ;;
    xlog)
        # log always follows both by default so nothing gets missed; pick one with 'log wsrx' / 'log web'.
        if [ "${targets}" = "wsrx web" ]; then
            touch "${WSRX_LOGFILE}" "${WEB_LOGFILE}"
            tail -f "${WSRX_LOGFILE}" "${WEB_LOGFILE}"
        elif [ "${targets}" = "wsrx" ]; then
            log_wsrx
        else
            log_web
        fi
        ;;
    *)
        usage
        exit 1
        ;;
esac

exit $rc
