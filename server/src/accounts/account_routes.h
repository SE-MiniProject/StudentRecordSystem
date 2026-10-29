#pragma once

namespace httplib {
class Server;
}

namespace srs {

class AccountStore;
class SessionStore;

// Registers the account administration routes (SRS-19 create, SRS-17 list and
// patch). Admin-only throughout: every one of these starts with the same RBAC
// guard, so an account cannot be created or deactivated by a forged request.
void RegisterAccountRoutes(httplib::Server& server, AccountStore& accounts,
                           SessionStore& sessions);

}  // namespace srs