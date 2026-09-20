pub mod archive_ops;
pub mod cli;
pub mod config;
pub mod converters;
pub mod file_converter;
pub mod file_converter_menus;
pub mod link_ops;
pub mod ui;
pub mod uploaders;

#[cfg(test)]
pub(crate) mod test_env {
    use std::sync::{Mutex, MutexGuard};

    static LOCK: Mutex<()> = Mutex::new(());

    /// Serializes tests that mutate process-global env (`DOLPHIN_*`, `PYTEST_*`).
    pub(crate) struct EnvGuard {
        _lock: MutexGuard<'static, ()>,
    }

    impl EnvGuard {
        pub(crate) fn lock() -> Self {
            let lock = LOCK.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
            Self { _lock: lock }
        }
    }

    impl Drop for EnvGuard {
        fn drop(&mut self) {
            // cargo test shares one process; never leave GUI unblocked for other threads.
            std::env::set_var("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1");
        }
    }
}
