type t('a) =
  | Ok('a)
  | Error(string);

/** [on_success] executes [f] unless we already hit an error. In
  that case the error is passed on. */;

/** [on_success] executes [f] unless we already hit an error. In
  that case the error is passed on. */
let on_success = (t: t('a), f: 'a => t('b)) =>
  switch (t) {
  | Ok(x) => f(x)
  | Error(str) => Error(str)
  };

/** [on_error] ignores the current error and executes [f]. If
  there is no error, [f] is not executed and the result is
  passed on. */;

/** [on_error] ignores the current error and executes [f]. If
  there is no error, [f] is not executed and the result is
  passed on. */
let on_error = (t, f) =>
  switch (t) {
  | Error(str) => f(str)
  | Ok(x) => Ok(x)
  };

let always = (_t, f) => f();

let ( /\/\* ) = always;

let error = fmt => Printf.kprintf(msg => Error(msg), fmt);

let return = x => Ok(x);

let fail = msg => Error(msg);

let (>>=) = on_success;
let (/\/=) = on_error;

let isDir = st =>
  switch (st.Unix.st_kind) {
  | Unix.S_DIR => return()
  | _ => error("not a directory")
  };

let hasOwner = (uid, st) =>
  if (st.Unix.st_uid == uid) {
    return();
  } else {
    error("expected uid = %d, found %d", uid, st.Unix.st_uid);
  };

let hasGroup = (gid, st) =>
  if (st.Unix.st_gid == gid) {
    return();
  } else {
    error("expected gid = %d, found %d", gid, st.Unix.st_gid);
  };

let hasPerm = (perm, st) =>
  if (st.Unix.st_perm == perm) {
    return();
  } else {
    error("expected permissions 0o%o, found 0o%o", perm, st.Unix.st_perm);
  };

let getgid = group =>
  Unix.(
    try (getgrnam(group).gr_gid |> return) {
    | Not_found => error("no such group: '%s'", group)
    }
  );

let getuid = user =>
  Unix.(
    try (getpwnam(user).pw_uid |> return) {
    | Not_found => error("no such user: '%s'", user)
    }
  );

let stat = path =>
  Unix.(
    try (Some(stat(path)) |> return) {
    | Unix_error(ENOENT, _, _) => return(None)
    }
  );

let chmod = (path, perm) =>
  Unix.(
    try (chmod(path, perm) |> return) {
    | Unix_error(_, _, _) => error("can't set permissions for '%s'", path)
    }
  );

let chown = (path, uid, gid) =>
  Unix.(
    try (chown(path, uid, gid) |> return) {
    | Unix_error(_, _, _) => error("can't set uid/gid for '%s'", path)
    }
  );

let mkdir = (path, perm) =>
  Unix.(
    try (mkdir(path, perm) |> return) {
    | Unix_error(_, _, _) => error("can't create directory '%s'", path)
    }
  );

let rmdir = path =>
  Unix.(
    try (rmdir(path) |> return) {
    | Unix_error(_, _, _) => error("can't remove directory '%s'", path)
    }
  );

let mk = (path, perm, user, group) =>
  getgid(group)
  >>= (
    gid =>
      getuid(user)
      >>= (
        uid =>
          stat(path)
          >>= (
            fun
            | Some(st) =>
              /* path already exists */
              isDir(st)
              >>= (
                () =>
                  hasOwner(uid, st)
                  /\/= (_ => chown(path, uid, gid))
                  >>= (
                    () =>
                      hasPerm(perm, st)
                      /\/= (_ => chmod(path, perm))
                      >>= (
                        () =>
                          hasGroup(gid, st)
                          /\/= (_ => chown(path, uid, gid))
                          /* improve error message, if we have an errror */
                          /\/= (
                            msg =>
                              error(
                                "fixing existing %s failed: %s",
                                path,
                                msg,
                              )
                          )
                      )
                  )
              )
            | None =>
              /* path does not exist */
              mkdir(path, perm)
              >>= (
                () =>
                  chown(path, uid, gid)
                  /\/= (msg => rmdir(path) /\/\* (() => fail(msg)))
                  /* improve error message, if we have an error */
                  /\/= (msg => error("creating %s failed: %s", path, msg))
              )
          )
      )
  );
