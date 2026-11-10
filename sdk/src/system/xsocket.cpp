/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <cstring>

#include <rex/kernel/xam/module.h>
#include <rex/platform.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xsocket.h>
// #include <rex/system/xnet.h>

#include <rex/net/socket.h>

// Standard socket types used by Xbox API emulation
#if REX_PLATFORM_WIN32
#include <WinSock2.h>

#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#endif

namespace rex::system {

namespace {
// scudrace-nx (2026-11-04): the guest's sockaddr_in (N_XSOCKADDR_IN: a 16-bit family, then the port and the address as
// big-endian numbers) to and from the host's. It used to be handed to the host as it was, which is Linux's and
// Windows's layout but not BSD's (Horizon: sin_len, then an 8-bit family: the family read as 0, so bind / connect /
// sendto with an address failed on the Switch), and SendTo / RecvFrom stored the port (and SendTo the address) as
// host numbers in network-order fields: byte-swapped (a broadcast to 255.255.255.255 survived that by symmetry).
sockaddr_in ToNative(const N_XSOCKADDR_IN* in) {
  sockaddr_in out;
  std::memset(&out, 0, sizeof(out));
#if defined(__SWITCH__) || defined(__APPLE__) || defined(__FreeBSD__)
  out.sin_len = sizeof(out);
#endif
  out.sin_family = AF_INET;
  out.sin_port = htons(uint16_t(in->sin_port));
  out.sin_addr.s_addr = htonl(uint32_t(in->sin_addr));
  return out;
}
void FromNative(const sockaddr_in& in, N_XSOCKADDR_IN* out) {
  out->sin_family = 2;
  out->sin_port = ntohs(in.sin_port);
  out->sin_addr = ntohl(in.sin_addr.s_addr);
  std::memset(out->x_sin_zero, 0, sizeof(out->x_sin_zero));
}
}  // namespace

XSocket::XSocket(KernelState* kernel_state) : XObject(kernel_state, kObjectType) {}

XSocket::XSocket(KernelState* kernel_state, uint64_t native_handle)
    : XObject(kernel_state, kObjectType), native_handle_(native_handle) {}

XSocket::~XSocket() {
  Close();
}

X_STATUS XSocket::Initialize(AddressFamily af, Type type, Protocol proto) {
  af_ = af;
  type_ = type;
  proto_ = proto;

  if (proto == Protocol::X_IPPROTO_VDP) {
    // VDP is a layer on top of UDP.
    proto = Protocol::X_IPPROTO_UDP;
  }

  native_handle_ = socket(af, type, proto);
  if (native_handle_ == -1) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Close() {
  int ret = rex::net::socket_close(native_handle_);
  if (ret != 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

// scudrace-nx (2026-11-04): the guest's options and values are Winsock's on the Xbox 360 (SOL_SOCKET 0xFFFF,
// SO_REUSEADDR 4, SO_BROADCAST 0x20, SO_SNDBUF 0x1001, SO_RCVBUF 0x1002, SO_SNDTIMEO 0x1005, SO_RCVTIMEO 0x1006 as a
// DWORD of milliseconds; IPPROTO_TCP 6 / TCP_NODELAY 1), and the value is a big-endian guest word. They were handed to
// the host as they were: right by chance on Windows and on Horizon (BSD numbering) except the timeouts (a timeval
// there) and the byte order, wrong on Linux (Scud Race's system link: SO_REUSEADDR / SO_BROADCAST failed, and its
// 3 s receive timeout failed everywhere, so a receiving thread blocked in recvfrom for good).
X_STATUS XSocket::SetOption(uint32_t level, uint32_t optname, void* optval_ptr, uint32_t optlen) {
  if (level == 0xFFFF && (optname == 0x5801 || optname == 0x5802)) {
    // Disable socket encryption
    secure_ = false;
    return X_STATUS_SUCCESS;
  }

#if REX_PLATFORM_WIN32
  int ret = setsockopt(native_handle_, level, optname, (char*)optval_ptr, optlen);
#else
  uint32_t value = 0;
  if (optval_ptr && optlen >= 4) {
    std::memcpy(&value, optval_ptr, 4);
    value = __builtin_bswap32(value);
  } else if (optval_ptr && optlen >= 1) {
    value = *static_cast<uint8_t*>(optval_ptr);
  }
  int native_level = int(level), native_name = -1;
  bool timeout = false;
  if (level == 0xFFFF) {
    native_level = SOL_SOCKET;
    switch (optname) {
      case 0x0004: native_name = SO_REUSEADDR; break;
      case 0x0008: native_name = SO_KEEPALIVE; break;
      case 0x0020: native_name = SO_BROADCAST; break;
      case 0x0080: native_name = SO_LINGER; break;
      case 0x1001: native_name = SO_SNDBUF; break;
      case 0x1002: native_name = SO_RCVBUF; break;
      case 0x1005: native_name = SO_SNDTIMEO; timeout = true; break;
      case 0x1006: native_name = SO_RCVTIMEO; timeout = true; break;
      default: break;
    }
  } else if (level == 6 && optname == 1) {
    native_level = IPPROTO_TCP;
    native_name = TCP_NODELAY;
  } else if (level == 0) {  // IPPROTO_IP: the Winsock and BSD numbers of the common ones agree
    native_level = IPPROTO_IP;
    native_name = int(optname);
  }
  int ret = -1;
  if (native_name == SO_LINGER) {
    ret = 0;  // the guest's linger struct is two big-endian u16s; the host default is what Xbox titles expect
  } else if (native_name >= 0 && timeout) {
    timeval tv;
    tv.tv_sec = value / 1000;
    tv.tv_usec = (value % 1000) * 1000;
    ret = setsockopt(native_handle_, native_level, native_name, &tv, sizeof(tv));
  } else if (native_name >= 0) {
    int v = int(value);
    ret = setsockopt(native_handle_, native_level, native_name, &v, sizeof(v));
  }
#endif
  if (ret < 0) {
    // TODO: WSAGetLastError()
    return X_STATUS_UNSUCCESSFUL;
  }

  // SO_BROADCAST
  if (level == 0xFFFF && optname == 0x0020) {
    broadcast_socket_ = true;
  }

  return X_STATUS_SUCCESS;
}

// scudrace-nx (2026-11-04): FIONBIO (0x8004667E) and FIONREAD (0x4004667F) with the guest's big-endian argument; the
// Winsock command numbers mean other things to Linux's ioctl (ENOTTY).
X_STATUS XSocket::IOControl(uint32_t cmd, uint8_t* arg_ptr) {
#if REX_PLATFORM_WIN32
  int ret = rex::net::socket_ioctl(native_handle_, cmd, arg_ptr);
#else
  int ret = -1;
  if (cmd == 0x8004667E && arg_ptr) {
    uint32_t value;
    std::memcpy(&value, arg_ptr, 4);
    const int flags = fcntl(int(native_handle_), F_GETFL, 0);
    ret = flags < 0 ? -1
                    : fcntl(int(native_handle_), F_SETFL,
                            __builtin_bswap32(value) ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
  } else if (cmd == 0x4004667F && arg_ptr) {
    int available = 0;
    ret = ioctl(int(native_handle_), FIONREAD, &available);
    const uint32_t be = __builtin_bswap32(uint32_t(available));
    std::memcpy(arg_ptr, &be, 4);
  } else {
    ret = rex::net::socket_ioctl(native_handle_, cmd, arg_ptr);
  }
#endif
  if (ret < 0) {
    // TODO: Get last error
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Connect(N_XSOCKADDR* name, int name_len) {
  sockaddr_in native = ToNative(reinterpret_cast<N_XSOCKADDR_IN*>(name));
  int ret = connect(native_handle_, (sockaddr*)&native, sizeof(native));
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Bind(N_XSOCKADDR_IN* name, int name_len) {
  sockaddr_in native = ToNative(name);
  int ret = bind(native_handle_, (sockaddr*)&native, sizeof(native));
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  bound_ = true;
  bound_port_ = name->sin_port;

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Listen(int backlog) {
  int ret = listen(native_handle_, backlog);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

object_ref<XSocket> XSocket::Accept(N_XSOCKADDR* name, int* name_len) {
  sockaddr n_sockaddr;
  socklen_t n_name_len = sizeof(sockaddr);
  uintptr_t ret = accept(native_handle_, &n_sockaddr, &n_name_len);
  if (ret == -1) {
    std::memset(name, 0, *name_len);
    *name_len = 0;
    return nullptr;
  }

  std::memcpy(name, &n_sockaddr, n_name_len);
  *name_len = n_name_len;

  // Create a kernel object to represent the new socket, and copy parameters
  // over.
  auto socket = object_ref<XSocket>(new XSocket(kernel_state_, ret));
  socket->af_ = af_;
  socket->type_ = type_;
  socket->proto_ = proto_;

  return socket;
}

int XSocket::Shutdown(int how) {
  return shutdown(native_handle_, how);
}

int XSocket::Recv(uint8_t* buf, uint32_t buf_len, uint32_t flags) {
  return recv(native_handle_, reinterpret_cast<char*>(buf), buf_len, flags);
}

int XSocket::RecvFrom(uint8_t* buf, uint32_t buf_len, uint32_t flags, N_XSOCKADDR_IN* from,
                      uint32_t* from_len) {
  // Pop from secure packets first
  // TODO(DrChat): Enable when I commit XNet
  /*
  {
    std::lock_guard<std::mutex> lock(incoming_packet_mutex_);
    if (incoming_packets_.size()) {
      packet* pkt = (packet*)incoming_packets_.front();
      int data_len = pkt->data_len;
      std::memcpy(buf, pkt->data, std::min((uint32_t)pkt->data_len, buf_len));

      from->sin_family = 2;
      from->sin_addr = pkt->src_ip;
      from->sin_port = pkt->src_port;

      incoming_packets_.pop();
      uint8_t* pkt_ui8 = (uint8_t*)pkt;
      delete[] pkt_ui8;

      return data_len;
    }
  }
  */

  sockaddr_in nfrom;
  socklen_t nfromlen = sizeof(sockaddr_in);
  int ret = recvfrom(native_handle_, reinterpret_cast<char*>(buf), buf_len, flags,
                     (sockaddr*)&nfrom, &nfromlen);
  if (from && ret >= 0) {
    FromNative(nfrom, from);
  }

  if (from_len) {
    *from_len = nfromlen;
  }

  return ret;
}

int XSocket::Send(const uint8_t* buf, uint32_t buf_len, uint32_t flags) {
  return send(native_handle_, reinterpret_cast<const char*>(buf), buf_len, flags);
}

int XSocket::SendTo(uint8_t* buf, uint32_t buf_len, uint32_t flags, N_XSOCKADDR_IN* to,
                    uint32_t to_len) {
  // Send 2 copies of the packet: One to XNet (for network security) and an
  // unencrypted copy for other Xenia hosts.
  // TODO(DrChat): Enable when I commit XNet.
  /*
  auto xam = kernel_state()->GetKernelModule<xam::XamModule>("xam.xex");
  auto xnet = xam->xnet();
  if (xnet) {
    xnet->SendPacket(this, to, buf, buf_len);
  }
  */

  sockaddr_in nto;
  if (to) nto = ToNative(to);

  return sendto(native_handle_, reinterpret_cast<char*>(buf), buf_len, flags,
                to ? (sockaddr*)&nto : nullptr, to ? socklen_t(sizeof(nto)) : 0);
}

bool XSocket::QueuePacket(uint32_t src_ip, uint16_t src_port, const uint8_t* buf, size_t len) {
  packet* pkt = reinterpret_cast<packet*>(new uint8_t[sizeof(packet) + len]);
  pkt->src_ip = src_ip;
  pkt->src_port = src_port;

  pkt->data_len = (uint16_t)len;
  std::memcpy(pkt->data, buf, len);

  std::lock_guard<std::mutex> lock(incoming_packet_mutex_);
  incoming_packets_.push((uint8_t*)pkt);

  // TODO: Limit on number of incoming packets?
  return true;
}

}  // namespace rex::system
