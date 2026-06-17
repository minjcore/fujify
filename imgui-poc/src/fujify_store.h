// fujify_store.h — SQLite-backed per-image edit state (the "data" layer).
//
// Each image's EditState lives as one row keyed by absolute path, so edits survive app
// restarts and scale to a large library (vs. an in-memory map lost on exit). Uses the
// system libsqlite3 (no extra dependency). All calls happen on the UI thread.
#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>

// Forward-uses EditState (defined in fujify_ui.h before this header is included).
struct EditState;

class StateStore {
public:
    void open(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) { db = nullptr; return; }
        const char* ddl =
            "CREATE TABLE IF NOT EXISTS edit_state("
            "path TEXT PRIMARY KEY, use_temp INT, wb_auto INT,"
            "temp REAL, tint REAL, br REAL, co REAL, sh REAL, hi REAL,"
            "preset INT, c0 REAL, c1 REAL, c2 REAL, c3 REAL, rotate INT)";
        sqlite3_exec(db, ddl, nullptr, nullptr, nullptr);
        const char* ddl2 =
            "CREATE TABLE IF NOT EXISTS snapshot("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, use_temp INT, wb_auto INT,"
            "temp REAL, tint REAL, br REAL, co REAL, sh REAL, hi REAL, preset INT)";
        sqlite3_exec(db, ddl2, nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    }
    ~StateStore() { if (db) sqlite3_close(db); }

    void save(const std::string& key, const EditState& e) {
        if (!db) return;
        const char* sql =
            "INSERT INTO edit_state VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
            "ON CONFLICT(path) DO UPDATE SET use_temp=excluded.use_temp,wb_auto=excluded.wb_auto,"
            "temp=excluded.temp,tint=excluded.tint,br=excluded.br,co=excluded.co,sh=excluded.sh,"
            "hi=excluded.hi,preset=excluded.preset,c0=excluded.c0,c1=excluded.c1,c2=excluded.c2,"
            "c3=excluded.c3,rotate=excluded.rotate";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return;
        sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (st, 2, e.use_temp); sqlite3_bind_int (st, 3, e.wb_auto);
        sqlite3_bind_double(st, 4, e.temp);   sqlite3_bind_double(st, 5, e.tint);
        sqlite3_bind_double(st, 6, e.br);     sqlite3_bind_double(st, 7, e.co);
        sqlite3_bind_double(st, 8, e.sh);     sqlite3_bind_double(st, 9, e.hi);
        sqlite3_bind_int (st, 10, e.preset);
        sqlite3_bind_double(st, 11, e.crop[0]); sqlite3_bind_double(st, 12, e.crop[1]);
        sqlite3_bind_double(st, 13, e.crop[2]); sqlite3_bind_double(st, 14, e.crop[3]);
        sqlite3_bind_int (st, 15, e.rotate);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    void remove(const std::string& key) {
        if (!db) return;
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, "DELETE FROM edit_state WHERE path=?", -1, &st, nullptr) != SQLITE_OK)
            return;
        sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    bool load(const std::string& key, EditState& e) {
        if (!db) return false;
        const char* sql = "SELECT use_temp,wb_auto,temp,tint,br,co,sh,hi,preset,"
                          "c0,c1,c2,c3,rotate FROM edit_state WHERE path=?";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        bool found = false;
        if (sqlite3_step(st) == SQLITE_ROW) {
            e.use_temp = sqlite3_column_int(st, 0) != 0;
            e.wb_auto  = sqlite3_column_int(st, 1) != 0;
            e.temp = (float)sqlite3_column_double(st, 2); e.tint = (float)sqlite3_column_double(st, 3);
            e.br   = (float)sqlite3_column_double(st, 4); e.co   = (float)sqlite3_column_double(st, 5);
            e.sh   = (float)sqlite3_column_double(st, 6); e.hi   = (float)sqlite3_column_double(st, 7);
            e.preset = sqlite3_column_int(st, 8);
            e.crop[0] = (float)sqlite3_column_double(st, 9);  e.crop[1] = (float)sqlite3_column_double(st, 10);
            e.crop[2] = (float)sqlite3_column_double(st, 11); e.crop[3] = (float)sqlite3_column_double(st, 12);
            e.rotate = sqlite3_column_int(st, 13);
            found = true;
        }
        sqlite3_finalize(st);
        return found;
    }

    // ---- snapshots: persistent look presets (not tied to an image) ----
    long add_snapshot(const EditState& e) {
        if (!db) return -1;
        const char* sql = "INSERT INTO snapshot(use_temp,wb_auto,temp,tint,br,co,sh,hi,preset) "
                          "VALUES(?,?,?,?,?,?,?,?,?)";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return -1;
        sqlite3_bind_int(st, 1, e.use_temp); sqlite3_bind_int(st, 2, e.wb_auto);
        sqlite3_bind_double(st, 3, e.temp); sqlite3_bind_double(st, 4, e.tint);
        sqlite3_bind_double(st, 5, e.br);   sqlite3_bind_double(st, 6, e.co);
        sqlite3_bind_double(st, 7, e.sh);   sqlite3_bind_double(st, 8, e.hi);
        sqlite3_bind_int(st, 9, e.preset);
        sqlite3_step(st);
        sqlite3_finalize(st);
        return (long)sqlite3_last_insert_rowid(db);
    }
    void load_snapshots(std::vector<EditState>& out, std::vector<long>& ids) {
        out.clear(); ids.clear();
        if (!db) return;
        sqlite3_stmt* st = nullptr;
        const char* sql = "SELECT id,use_temp,wb_auto,temp,tint,br,co,sh,hi,preset "
                          "FROM snapshot ORDER BY id";
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return;
        while (sqlite3_step(st) == SQLITE_ROW) {
            ids.push_back((long)sqlite3_column_int64(st, 0));
            EditState e;
            e.use_temp = sqlite3_column_int(st, 1) != 0; e.wb_auto = sqlite3_column_int(st, 2) != 0;
            e.temp = (float)sqlite3_column_double(st, 3); e.tint = (float)sqlite3_column_double(st, 4);
            e.br = (float)sqlite3_column_double(st, 5); e.co = (float)sqlite3_column_double(st, 6);
            e.sh = (float)sqlite3_column_double(st, 7); e.hi = (float)sqlite3_column_double(st, 8);
            e.preset = sqlite3_column_int(st, 9);
            out.push_back(e);
        }
        sqlite3_finalize(st);
    }
    void del_snapshot(long id) {
        if (!db) return;
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, "DELETE FROM snapshot WHERE id=?", -1, &st, nullptr) != SQLITE_OK) return;
        sqlite3_bind_int64(st, 1, id); sqlite3_step(st); sqlite3_finalize(st);
    }
    void clear_snapshots() { if (db) sqlite3_exec(db, "DELETE FROM snapshot", nullptr, nullptr, nullptr); }

private:
    sqlite3* db = nullptr;
};
