#include "oneflow/core/persistence/snapshot.h"
#include "oneflow/core/common/balanced_splitter.h"
#include "oneflow/core/common/str_util.h"
#include "oneflow/core/job/job_desc.h"
#include "oneflow/core/operator/operator.h"

namespace oneflow {

Snapshot::Snapshot(const std::string& snapshot_root_path) {
  FS_CHECK_OK(GlobalFS()->IsDirectory(snapshot_root_path));
  root_path_ = snapshot_root_path;
}

std::unique_ptr<NormalPersistentInStream> Snapshot::GetInStream(
    const std::string& lbn, size_t begin_pos) const {
  std::string file_path = JoinPath(root_path_, lbn);
  return of_make_unique<NormalPersistentInStream>(GlobalFS(), file_path,
                                                  begin_pos);
}

std::unique_ptr<NormalPersistentInStream> Snapshot::GetInStream(
    const std::string& lbn, int32_t part_id, int32_t part_num, int32_t dim_num,
    int64_t byte_size_of_each_dim) const {
  std::string file_path = JoinPath(root_path_, lbn);
  uint64_t file_size = 0;
  FS_CHECK_OK(GlobalFS()->GetFileSize(file_path, &file_size));
  CHECK_EQ(file_size, dim_num * byte_size_of_each_dim);
  BalancedSplitter splitter = BalancedSplitter(dim_num, part_num);
  int64_t begin_pos = splitter.At(part_id).begin() * byte_size_of_each_dim;
  return GetInStream(lbn, begin_pos);
}

std::unique_ptr<PersistentOutStream> Snapshot::GetOutStream(
    const std::string& lbn, int32_t part_id) {
  // parse lbn
  std::pair<std::string, std::string> parsed_lbn = ParseLbn(lbn);
  const std::string& op_name = parsed_lbn.first;
  const std::string& bn_in_op = parsed_lbn.second;
  // op_name_dir
  std::string op_name_dir = JoinPath(root_path_, op_name);
  OF_ONCE_GUARD(op_name_dir, FS_CHECK_OK(GlobalFS()->CreateDir(op_name_dir)));
  // bn_in_op_tmp_dir
  std::string bn_in_op_tmp_dir = JoinPath(op_name_dir, bn_in_op + "_tmp");
  OF_ONCE_GUARD(bn_in_op_tmp_dir,
                FS_CHECK_OK(GlobalFS()->CreateDir(bn_in_op_tmp_dir)));
  // part_file
  std::string part_file =
      JoinPath(bn_in_op_tmp_dir, "part_" + std::to_string(part_id));
  return of_make_unique<PersistentOutStream>(GlobalFS(), part_file);
}

void Snapshot::OnePartDone(const std::string& lbn, int32_t part_id,
                           int32_t part_num) {
  std::string done_dir = JoinPath(root_path_, lbn + "_done");
  OF_ONCE_GUARD(done_dir, FS_CHECK_OK(GlobalFS()->CreateDir(done_dir)));
  std::string done_file_path = JoinPath(done_dir, std::to_string(part_id));
  CHECK_EQ(GlobalFS()->FileExists(done_file_path), fs::Status::NOT_FOUND);
  { PersistentOutStream out_stream(GlobalFS(), done_file_path); }
  std::vector<std::string> done_files;
  GlobalFS()->GetChildren(done_dir, &done_files);
  if (done_files.size() == part_num) {
    std::string concat_file = JoinPath(root_path_, lbn);
    OF_ONCE_GUARD(concat_file, int64_t undeleted_files = 0;
                  int64_t undeleted_dirs = 0;
                  FS_CHECK_OK(GlobalFS()->DeleteRecursively(
                      done_dir, &undeleted_files, &undeleted_dirs));
                  CHECK_EQ(undeleted_files, 0); CHECK_EQ(undeleted_dirs, 0);
                  ConcatLbnFile(lbn, part_num, concat_file));
  }
}

void Snapshot::ConcatLbnFile(const std::string& lbn, int32_t part_num,
                             const std::string& concat_file) {
  static const uint64_t buffer_size = 256 * 1024 * 1024;
  static char* buffer = new char[buffer_size];
  std::pair<std::string, std::string> parsed_lbn = ParseLbn(lbn);
  const std::string& op_name = parsed_lbn.first;
  const std::string& bn_in_op = parsed_lbn.second;
  std::string part_dir = JoinPath(root_path_, lbn + "_tmp");
  {
    PersistentOutStream out_stream(GlobalFS(), concat_file);
    for (int32_t i = 0; i < part_num; ++i) {
      std::unique_ptr<fs::RandomAccessFile> part_file;
      std::string part_file_path =
          JoinPath(part_dir, "part_" + std::to_string(i));
      FS_CHECK_OK(GlobalFS()->NewRandomAccessFile(part_file_path, &part_file));
      uint64_t part_file_size = 0;
      FS_CHECK_OK(GlobalFS()->GetFileSize(part_file_path, &part_file_size));
      uint64_t offset = 0;
      while (offset < part_file_size) {
        uint64_t n = std::min(buffer_size, part_file_size - offset);
        FS_CHECK_OK(part_file->Read(offset, n, buffer));
        out_stream.Write(buffer, n);
        offset += n;
      }
      FS_CHECK_OK(GlobalFS()->DeleteFile(part_file_path));
    }
  }
  FS_CHECK_OK(GlobalFS()->DeleteDir(part_dir));
  std::string done_dir = JoinPath(root_path_, op_name, "done");
  OF_ONCE_GUARD(done_dir, FS_CHECK_OK(GlobalFS()->CreateDir(done_dir)));
  PersistentOutStream out_stream(GlobalFS(), JoinPath(done_dir, bn_in_op));
}

}  // namespace oneflow
