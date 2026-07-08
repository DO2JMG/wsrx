#!/bin/bash

# wsrx-web start script
# Starts the separate web companion in the background.

workingdir="$( cd "$(dirname "$0")" ; pwd -P )"
piddir="${workingdir}/pidfiles"
logdir="${workingdir}/logs"
WEB="${workingdir}/wsrx-web"
PIDFILE="${piddir}/wsrx-web.pid"
LOGFILE="${logdir}/wsrx-web.log"
BIND="${WSRX_WEB_BIND:-0.0.0.0}"
PORT="${WSRX_WEB_PORT:-8073}"

mkdir -p "${piddir}" "${logdir}"

function timestamp {
    date "+%Y-%m-%d %H:%M:%S"
}

function print_urls {
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

function print_listen {
    if command -v ss >/dev/null 2>&1; then
        echo "$(timestamp) [INFO] listening sockets for :${PORT}:"
        ss -ltnp 2>/dev/null | grep ":${PORT} " || true
    elif command -v netstat >/dev/null 2>&1; then
        echo "$(timestamp) [INFO] listening sockets for :${PORT}:"
        netstat -ltnp 2>/dev/null | grep ":${PORT} " || true
    fi
}

function check_binary {
    if [ ! -x "${WEB}" ]; then
        echo "$(timestamp) [ERROR] missing executable: ${WEB}" >&2
        echo "Build it first with: cd ${workingdir} && make" >&2
        exit 1
    fi
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

function is_running {
    if [ -s "${PIDFILE}" ]; then
        pid="$(cat "${PIDFILE}")"
        if pid_is_wsrx_web "${pid}"; then
            return 0
        fi
    fi
    return 1
}

function sanitycheck {
    if [ -s "${PIDFILE}" ]; then
        pid="$(cat "${PIDFILE}")"
        if ! pid_is_wsrx_web "${pid}"; then
            echo "$(timestamp) [WARN] stale pidfile removed: ${PIDFILE}"
            rm -f "${PIDFILE}"
        fi
    fi
}

function port_pids {
    if command -v ss >/dev/null 2>&1; then
        ss -ltnp 2>/dev/null | awk -v p=":${PORT}" '$4 ~ p {print $0}' | sed -n 's/.*pid=\([0-9][0-9]*\).*/\1/p' | sort -u
    elif command -v lsof >/dev/null 2>&1; then
        lsof -tiTCP:"${PORT}" -sTCP:LISTEN 2>/dev/null | sort -u
    fi
}

function stop_pid {
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
    for pid in $(port_pids); do
        if pid_is_wsrx_web "${pid}"; then
            stop_pid "${pid}"
        else
            echo "$(timestamp) [ERROR] port ${PORT} is used by another process pid=${pid}" >&2
            print_listen >&2
            return 1
        fi
    done
    return 0
}

function start_web {
    check_binary
    sanitycheck

    if is_running; then
        pid="$(cat "${PIDFILE}")"
        echo "$(timestamp) [INFO] wsrx-web already running pid=${pid}"
        print_urls
        return 0
    fi

    cleanup_old_web_processes || return 1

    echo "$(timestamp) [INFO] starting wsrx-web bind=${BIND} port=${PORT}"
    cd "${workingdir}" || exit 1
    nohup "${WEB}" -bind "${BIND}" -port "${PORT}" -dir "${workingdir}" >> "${LOGFILE}" 2>&1 &
    pid=$!
    echo "${pid}" > "${PIDFILE}"
    sleep 1

    if is_running; then
        echo "$(timestamp) [INFO] wsrx-web started pid=${pid} log=${LOGFILE}"
        print_urls
        print_listen
        return 0
    fi

    if grep -q "Address already in use" "${LOGFILE}" 2>/dev/null; then
        echo "$(timestamp) [ERROR] port ${PORT} is already in use" >&2
        print_listen >&2
    else
        echo "$(timestamp) [ERROR] wsrx-web did not stay running. See log: ${LOGFILE}" >&2
    fi
    rm -f "${PIDFILE}"
    return 1
}

function stop_web {
    sanitycheck
    if is_running; then
        pid="$(cat "${PIDFILE}")"
        stop_pid "${pid}"
    else
        echo "$(timestamp) [INFO] wsrx-web is not running"
    fi
    rm -f "${PIDFILE}"

    # Also remove stale wsrx-web instances on the same port.
    for pid in $(port_pids); do
        if pid_is_wsrx_web "${pid}"; then
            stop_pid "${pid}"
        fi
    done
}

function status_web {
    sanitycheck
    if is_running; then
        pid="$(cat "${PIDFILE}")"
        echo "$(timestamp) [INFO] wsrx-web running pid=${pid}"
        print_urls
        print_listen
        return 0
    fi
    echo "$(timestamp) [INFO] wsrx-web not running"
    print_listen
    return 1
}

case "x$1" in
    xstart|x)
        start_web
        ;;
    xstop)
        stop_web
        ;;
    xrestart)
        stop_web
        start_web
        ;;
    xstatus)
        status_web
        ;;
    xlog)
        touch "${LOGFILE}"
        tail -f "${LOGFILE}"
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status|log}"
        echo "Environment: WSRX_WEB_BIND=0.0.0.0 WSRX_WEB_PORT=8073"
        exit 1
        ;;
esac

exit $?
