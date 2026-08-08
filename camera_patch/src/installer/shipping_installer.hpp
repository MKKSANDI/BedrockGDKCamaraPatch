#pragma once

namespace mcfix::installer {

int shipping_status(bool verify_only);
int shipping_install(bool repair, bool silent);
int shipping_uninstall(bool silent);
int shipping_watch();

}  // namespace mcfix::installer
