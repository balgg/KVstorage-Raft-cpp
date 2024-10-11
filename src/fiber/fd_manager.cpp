// 文件描述符FD管理器
// 用于在网络编程中管理文件描述符的状态和行为
// 包括两个主要的类：FdCtx和dManager
/*
* 这个文件描述符管理器可以在需要高效管理大量文件描述符的网络服务器或代理中使用。
通过使用读写锁，它能够支持高并发的访问，同时通过非阻塞模式提高I/O操作的效率。
*/

#include "fd_manager.hpp"
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "hook.hpp"

namespace monsoon {

FdCtx::FdCtx(int fd)
    : m_isInit(false),
      m_isSocket(false),
      m_sysNonblock(false),
      m_userNonblock(false),
      m_isClosed(false),
      m_fd(fd),
      m_recvTimeout(-1),
      m_sendTimeout(-1) {
  init();
}

FdCtx::~FdCtx() {}

bool FdCtx::init() {
  if (m_isInit) {
    return true;
  }
  m_recvTimeout = -1;
  m_sendTimeout = -1;

  // 获取文件状态信息
  struct stat fd_stat;
  if (-1 == fstat(m_fd, &fd_stat)) {
    m_isInit = false;
    m_isSocket = false;
  } else {
    m_isInit = true;
    // 判断是否是socket
    m_isSocket = S_ISSOCK(fd_stat.st_mode);
  }

  // 对socket设置非阻塞
  if (m_isSocket) {
    int flags = fcntl_f(m_fd, F_GETFL, 0);
    if (!(flags & O_NONBLOCK)) {
      fcntl_f(m_fd, F_SETFL, flags | O_NONBLOCK);
    }
    m_sysNonblock = true;
  } else {
    m_sysNonblock = false;
  }

  m_userNonblock = false;
  m_isClosed = false;
  return m_isInit;
}

void FdCtx::setTimeout(int type, uint64_t v) {
  if (type == SO_RCVTIMEO) {
    m_recvTimeout = v;
  } else {
    m_sendTimeout = v;
  }
}

uint64_t FdCtx::getTimeout(int type) {
  if (type == SO_RCVTIMEO) {
    return m_recvTimeout;
  } else {
    return m_sendTimeout;
  }
}

// FdManager类用于管理所有fd的上下文信息（用动态数组m_datas来存储fd的上下文指针）
// 构造函数：初始化动态数组的大小
FdManager::FdManager() { m_datas.resize(64); }
// get：获取指定fd的上下文
FdCtx::ptr FdManager::get(int fd, bool auto_create) {
  if (fd == -1) {
    return nullptr;
  }
  RWMutexType::ReadLock lock(m_mutex);  // RWMutexType读写锁类型
  if ((int)m_datas.size() <= fd) {
    if (auto_create == false) {
      return nullptr;
    }
  } else {
    if (m_datas[fd] || !auto_create) {  
      return m_datas[fd];
    }
  }
  lock.unlock();

  RWMutexType::WriteLock lock2(m_mutex);
  FdCtx::ptr ctx(new FdCtx(fd));
  if (fd >= (int)m_datas.size()) {
    m_datas.resize(fd * 1.5);
  }
  m_datas[fd] = ctx;
  return ctx;
}

void FdManager::del(int fd) {
  RWMutexType::WriteLock lock(m_mutex);
  if ((int)m_datas.size() <= fd) {
    return;
  }
  m_datas[fd].reset();
}
}  // namespace monsoon