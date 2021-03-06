// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/tests/test_file_ref.h"

#include <stdio.h>

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/dev/ppb_file_io_dev.h"
#include "ppapi/c/dev/ppb_testing_dev.h"
#include "ppapi/cpp/dev/file_io_dev.h"
#include "ppapi/cpp/dev/file_ref_dev.h"
#include "ppapi/cpp/dev/file_system_dev.h"
#include "ppapi/cpp/dev/url_loader_dev.h"
#include "ppapi/cpp/dev/url_request_info_dev.h"
#include "ppapi/cpp/dev/url_response_info_dev.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/module.h"
#include "ppapi/tests/testing_instance.h"

REGISTER_TEST_CASE(FileRef);

namespace {

const char* kPersFileName = "persistent";
const char* kTempFileName = "temporary";
const char* kParentPath = "/foo/bar";
const char* kPersFilePath = "/foo/bar/persistent";
const char* kTempFilePath = "/foo/bar/temporary";

const PPB_Testing_Dev* g_testing_interface;

class TestCompletionCallback {
 public:
  TestCompletionCallback() : result_(PP_ERROR_WOULDBLOCK) {
  }

  operator pp::CompletionCallback() const {
    return pp::CompletionCallback(&TestCompletionCallback::Handler,
                                  const_cast<TestCompletionCallback*>(this));
  }

  int32_t WaitForResult() {
    result_ = PP_ERROR_WOULDBLOCK;  // Reset
    g_testing_interface->RunMessageLoop();
    return result_;
  }

 private:
  static void Handler(void* user_data, int32_t result) {
    static_cast<TestCompletionCallback*>(user_data)->result_ = result;
    g_testing_interface->QuitMessageLoop();
  }

  int32_t result_;
};

std::string ReportMismatch(const std::string& method_name,
                           const std::string& returned_result,
                           const std::string& expected_result) {
  return method_name + " returned '" + returned_result + "'; '" +
      expected_result + "' expected.";
}

std::string ReportError(const char* method, int32_t error) {
  char error_as_string[12];
  sprintf(error_as_string, "%d", error);
  std::string result = method + std::string(" failed with error: ") +
      error_as_string;
  if (error == PP_ERROR_NOSPACE)
    result += ". Did you run the test with --unlimited-quota-for-files?";
  return result;
}

}  // namespace

bool TestFileRef::Init() {
  g_testing_interface = reinterpret_cast<PPB_Testing_Dev const*>(
      pp::Module::Get()->GetBrowserInterface(PPB_TESTING_DEV_INTERFACE));
  if (!g_testing_interface) {
    // Give a more helpful error message for the testing interface being gone
    // since that needs special enabling in Chrome.
    instance_->AppendError("This test needs the testing interface, which is "
        "not currently available. In Chrome, use --enable-pepper-testing when "
        "launching.");
    return false;
  }

  // Make sure we're running over HTTP.
  pp::Var window = instance_->GetWindowObject();
  pp::Var location = window.GetProperty("location");
  pp::Var protocol = location.GetProperty("protocol");
  if (!protocol.is_string() || protocol.AsString() != "http:") {
    instance_->AppendError("This test needs to be run over HTTP.");
    return false;
  }

  return true;
}

void TestFileRef::RunTest() {
  RUN_TEST(GetFileSystemType);
  RUN_TEST(GetName);
  RUN_TEST(GetPath);
  RUN_TEST(GetParent);
  RUN_TEST(MakeDirectory);
  RUN_TEST(QueryAndTouchFile);
  RUN_TEST(DeleteFileAndDirectory);
  RUN_TEST(RenameFileAndDirectory);
}

std::string TestFileRef::TestGetFileSystemType() {
  pp::FileSystem_Dev file_system_pers(
      instance_, PP_FILESYSTEMTYPE_LOCALPERSISTENT);
  pp::FileSystem_Dev file_system_temp(
      instance_, PP_FILESYSTEMTYPE_LOCALTEMPORARY);

  pp::FileRef_Dev file_ref_pers(file_system_pers, kPersFilePath);
  if (file_ref_pers.GetFileSystemType() != PP_FILESYSTEMTYPE_LOCALPERSISTENT)
    return "file_ref_pers expected to be persistent.";

  pp::FileRef_Dev file_ref_temp(file_system_temp, kTempFilePath);
  if (file_ref_temp.GetFileSystemType() != PP_FILESYSTEMTYPE_LOCALTEMPORARY)
    return "file_ref_temp expected to be temporary.";

  pp::URLRequestInfo_Dev request;
  request.SetURL("test_url_loader_data/hello.txt");
  request.SetStreamToFile(true);

  TestCompletionCallback callback;

  pp::URLLoader_Dev loader(*instance_);
  int32_t rv = loader.Open(request, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return "URLLoader::Open() failed.";

  pp::URLResponseInfo_Dev response_info(loader.GetResponseInfo());
  if (response_info.is_null())
    return "URLLoader::GetResponseInfo returned null";
  int32_t status_code = response_info.GetStatusCode();
  if (status_code != 200)
    return "Unexpected HTTP status code";

  pp::FileRef_Dev file_ref_ext(response_info.GetBody());
  if (file_ref_ext.GetFileSystemType() != PP_FILESYSTEMTYPE_EXTERNAL)
    return "file_ref_ext expected to be external.";

  return "";
}

std::string TestFileRef::TestGetName() {
  pp::FileSystem_Dev file_system_pers(
      instance_, PP_FILESYSTEMTYPE_LOCALPERSISTENT);
  pp::FileSystem_Dev file_system_temp(
      instance_, PP_FILESYSTEMTYPE_LOCALTEMPORARY);

  pp::FileRef_Dev file_ref_pers(file_system_pers, kPersFilePath);
  std::string name = file_ref_pers.GetName().AsString();
  if (name != kPersFileName)
    return ReportMismatch("FileRef::GetName", name, kPersFileName);

  pp::FileRef_Dev file_ref_temp(file_system_temp, kTempFilePath);
  name = file_ref_temp.GetName().AsString();
  if (name != kTempFileName)
    return ReportMismatch("FileRef::GetName", name, kTempFileName);

  // Test the "/" case.
  pp::FileRef_Dev file_ref_slash(file_system_temp, "/");
  name = file_ref_slash.GetName().AsString();
  if (name != "/")
    return ReportMismatch("FileRef::GetName", name, "/");

  pp::URLRequestInfo_Dev request;
  request.SetURL("test_url_loader_data/hello.txt");
  request.SetStreamToFile(true);

  TestCompletionCallback callback;

  pp::URLLoader_Dev loader(*instance_);
  int32_t rv = loader.Open(request, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return "URLLoader::Open() failed.";

  pp::URLResponseInfo_Dev response_info(loader.GetResponseInfo());
  if (response_info.is_null())
    return "URLLoader::GetResponseInfo returned null";
  int32_t status_code = response_info.GetStatusCode();
  if (status_code != 200)
    return "Unexpected HTTP status code";

  pp::FileRef_Dev file_ref_ext(response_info.GetBody());
  name = file_ref_ext.GetName().AsString();
  if (name != "")
    return ReportMismatch("FileRef::GetName", name, "<empty string>");

  return "";
}

std::string TestFileRef::TestGetPath() {
  pp::FileSystem_Dev file_system_pers(
      instance_, PP_FILESYSTEMTYPE_LOCALPERSISTENT);
  pp::FileSystem_Dev file_system_temp(
      instance_, PP_FILESYSTEMTYPE_LOCALTEMPORARY);

  pp::FileRef_Dev file_ref_pers(file_system_pers, kPersFilePath);
  std::string path = file_ref_pers.GetPath().AsString();
  if (path != kPersFilePath)
    return ReportMismatch("FileRef::GetPath", path, kPersFilePath);

  pp::FileRef_Dev file_ref_temp(file_system_temp, kTempFilePath);
  path = file_ref_temp.GetPath().AsString();
  if (path != kTempFilePath)
    return ReportMismatch("FileRef::GetPath", path, kTempFilePath);

  pp::URLRequestInfo_Dev request;
  request.SetURL("test_url_loader_data/hello.txt");
  request.SetStreamToFile(true);

  TestCompletionCallback callback;

  pp::URLLoader_Dev loader(*instance_);
  int32_t rv = loader.Open(request, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return "URLLoader::Open() failed.";

  pp::URLResponseInfo_Dev response_info(loader.GetResponseInfo());
  if (response_info.is_null())
    return "URLLoader::GetResponseInfo returned null";
  int32_t status_code = response_info.GetStatusCode();
  if (status_code != 200)
    return "Unexpected HTTP status code";

  pp::FileRef_Dev file_ref_ext(response_info.GetBody());
  if (!file_ref_ext.GetPath().is_undefined())
    return "The path of an external FileRef should be void.";

  return "";
}

std::string TestFileRef::TestGetParent() {
  pp::FileSystem_Dev file_system_pers(
      instance_, PP_FILESYSTEMTYPE_LOCALPERSISTENT);
  pp::FileSystem_Dev file_system_temp(
      instance_, PP_FILESYSTEMTYPE_LOCALTEMPORARY);

  pp::FileRef_Dev file_ref_pers(file_system_pers, kPersFilePath);
  std::string parent_path = file_ref_pers.GetParent().GetPath().AsString();
  if (parent_path != kParentPath)
    return ReportMismatch("FileRef::GetParent", parent_path, kParentPath);

  pp::FileRef_Dev file_ref_temp(file_system_temp, kTempFilePath);
  parent_path = file_ref_temp.GetParent().GetPath().AsString();
  if (parent_path != kParentPath)
    return ReportMismatch("FileRef::GetParent", parent_path, kParentPath);

  // Test the "/" case.
  pp::FileRef_Dev file_ref_slash(file_system_temp, "/");
  parent_path = file_ref_slash.GetParent().GetPath().AsString();
  if (parent_path != "/")
    return ReportMismatch("FileRef::GetParent", parent_path, "/");

  // Test the "/foo" case (the parent is "/").
  pp::FileRef_Dev file_ref_with_root_parent(file_system_temp, "/foo");
  parent_path = file_ref_with_root_parent.GetParent().GetPath().AsString();
  if (parent_path != "/")
    return ReportMismatch("FileRef::GetParent", parent_path, "/");

  pp::URLRequestInfo_Dev request;
  request.SetURL("test_url_loader_data/hello.txt");
  request.SetStreamToFile(true);

  TestCompletionCallback callback;

  pp::URLLoader_Dev loader(*instance_);
  int32_t rv = loader.Open(request, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return "URLLoader::Open() failed.";

  pp::URLResponseInfo_Dev response_info(loader.GetResponseInfo());
  if (response_info.is_null())
    return "URLLoader::GetResponseInfo returned null";
  int32_t status_code = response_info.GetStatusCode();
  if (status_code != 200)
    return "Unexpected HTTP status code";

  pp::FileRef_Dev file_ref_ext(response_info.GetBody());
  if (!file_ref_ext.GetParent().is_null())
    return "The parent of an external FileRef should be null.";

  return "";
}

std::string TestFileRef::TestMakeDirectory() {
  TestCompletionCallback callback;

  pp::FileSystem_Dev file_system(instance_, PP_FILESYSTEMTYPE_LOCALTEMPORARY);
  int32_t rv = file_system.Open(1024, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::Open", rv);

  pp::FileRef_Dev dir_ref(file_system, "/test_dir_make_directory");
  rv = dir_ref.MakeDirectory(callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::MakeDirectory", rv);

  dir_ref = pp::FileRef_Dev(file_system, "/dir_make_dir_1/dir_make_dir_2");
  rv = dir_ref.MakeDirectoryIncludingAncestors(callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::MakeDirectoryIncludingAncestors", rv);

  dir_ref = pp::FileRef_Dev(file_system, "/dir_make_dir_3/dir_make_dir_4");
  rv = dir_ref.MakeDirectory(callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv == PP_OK)
    return "Calling FileSystem::MakeDirectory() with a nested directory path " \
        "should have failed.";

  return "";
}

std::string TestFileRef::TestQueryAndTouchFile() {
  TestCompletionCallback callback;
  pp::FileSystem_Dev file_system(instance_, PP_FILESYSTEMTYPE_LOCALTEMPORARY);
  int32_t rv = file_system.Open(1024, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::Open", rv);

  pp::FileRef_Dev file_ref(file_system, "/file_touch");
  pp::FileIO_Dev file_io;
  rv = file_io.Open(file_ref,
                    PP_FILEOPENFLAG_CREATE | PP_FILEOPENFLAG_WRITE,
                    callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileIO::Open", rv);

  // Write some data to have a non-zero file size.
  rv = file_io.Write(0, "test", 4, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != 4)
    return ReportError("FileIO::Write", rv);

  // last_access_time's granularity is 1 day
  // last_modified_time's granularity is 2 seconds
  const PP_Time last_access_time = 123 * 24 * 3600.0;
  const PP_Time last_modified_time = 246.0;
  rv = file_ref.Touch(last_access_time, last_modified_time, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::Touch", rv);

  PP_FileInfo_Dev info;
  rv = file_ref.Query(&info, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::Query", rv);

  if ((info.size != 4) ||
      (info.type != PP_FILETYPE_REGULAR) ||
      (info.system_type != PP_FILESYSTEMTYPE_LOCALTEMPORARY) ||
      (info.last_access_time != last_access_time) ||
      (info.last_modified_time != last_modified_time))
    return "FileSystem::Query() has returned bad data.";

  return "";
}

std::string TestFileRef::TestDeleteFileAndDirectory() {
  TestCompletionCallback callback;
  pp::FileSystem_Dev file_system(instance_, PP_FILESYSTEMTYPE_LOCALTEMPORARY);
  int32_t rv = file_system.Open(1024, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::Open", rv);

  pp::FileRef_Dev file_ref(file_system, "/file_delete");
  pp::FileIO_Dev file_io;
  rv = file_io.Open(file_ref, PP_FILEOPENFLAG_CREATE, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileIO::Open", rv);

  rv = file_ref.Delete(callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::Delete", rv);

  pp::FileRef_Dev dir_ref(file_system, "/dir_delete");
  rv = dir_ref.MakeDirectory(callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::MakeDirectory", rv);

  rv = dir_ref.Delete(callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::Delete", rv);

  pp::FileRef_Dev nested_dir_ref(file_system, "/dir_delete_1/dir_delete_2");
  rv = nested_dir_ref.MakeDirectoryIncludingAncestors(callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::MakeDirectoryIncludingAncestors", rv);

  rv = nested_dir_ref.GetParent().Delete(callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_ERROR_FAILED)
    return ReportError("FileSystem::Delete", rv);

  pp::FileRef_Dev nonexistent_file_ref(file_system, "/nonexistent_file_delete");
  rv = nonexistent_file_ref.Delete(callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_ERROR_FILENOTFOUND)
    return ReportError("FileSystem::Delete", rv);

  return "";
}

std::string TestFileRef::TestRenameFileAndDirectory() {
  TestCompletionCallback callback;
  pp::FileSystem_Dev file_system(instance_, PP_FILESYSTEMTYPE_LOCALTEMPORARY);
  int32_t rv = file_system.Open(1024, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::Open", rv);

  pp::FileRef_Dev file_ref(file_system, "/file_rename");
  pp::FileIO_Dev file_io;
  rv = file_io.Open(file_ref, PP_FILEOPENFLAG_CREATE, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileIO::Open", rv);

  pp::FileRef_Dev target_file_ref(file_system, "/target_file_rename");
  rv = file_ref.Rename(target_file_ref, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::Rename", rv);

  pp::FileRef_Dev dir_ref(file_system, "/dir_rename");
  rv = dir_ref.MakeDirectory(callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::MakeDirectory", rv);

  pp::FileRef_Dev target_dir_ref(file_system, "/target_dir_rename");
  rv = dir_ref.Rename(target_dir_ref, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::Rename", rv);

  pp::FileRef_Dev nested_dir_ref(file_system, "/dir_rename_1/dir_rename_2");
  rv = nested_dir_ref.MakeDirectoryIncludingAncestors(callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_OK)
    return ReportError("FileSystem::MakeDirectoryIncludingAncestors", rv);

  pp::FileRef_Dev target_nested_dir_ref(file_system, "/dir_rename_1");
  rv = nested_dir_ref.Rename(target_nested_dir_ref, callback);
  if (rv == PP_ERROR_WOULDBLOCK)
    rv = callback.WaitForResult();
  if (rv != PP_ERROR_FAILED)
    return ReportError("FileSystem::Rename", rv);

  return "";
}
