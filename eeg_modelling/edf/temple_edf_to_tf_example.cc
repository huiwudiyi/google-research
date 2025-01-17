// Utility to convert a Temple EEG file in EDF format to TFRecord format.

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/internal/raw_logging.h"
#include "absl/container/flat_hash_map.h"
#include "edf/edf_oss_file.h"
#include "edf/parse_edf_lib.h"
#include "edf/proto/segment.pb.h"
#include "edf/temple_util.h"
#include "edf/tf_example_lib.h"
#include "lullaby/util/arg_parser.h"
#include "lullaby/util/string_view.h"
#include "tensorflow/core/example/example.pb.h"
#include "tensorflow/core/lib/core/status.h"
#include "tensorflow/core/lib/io/record_reader.h"
#include "tensorflow/core/lib/io/record_writer.h"

using eeg_modelling::Annotations;
using eeg_modelling::Segment;

using std::string;

int main(int argc, const char* argv[]) {
  lull::ArgParser args;
  // clang-format off
  args.AddArg("output_path")
      .SetNumArgs(1)
      .SetDefault(".")
      .SetDescription("Location (path) to save output tf Records file");
  args.AddArg("edf_path")
      .SetNumArgs(1)
      .SetDefault("")
      .SetDescription("Path to EDF file.");
  args.AddArg("edf_annotation_path")
      .SetNumArgs(1)
      .SetDefault("")
      .SetDescription("Path to EDF annotation file.");
  args.AddArg("help")
      .SetShortName('h')
      .SetDescription("Usage Information");

  // Parse the command-line arguments.
  if (!args.Parse(argc, argv)) {
    auto& errors = args.GetErrors();
    for (auto& err : errors) {
      ABSL_RAW_LOG(ERROR, "Error in argument %s ", err.c_str());
    }
    return -1;
  }

  if (args.IsSet("help")) {
    ABSL_RAW_LOG(ERROR,
                 "Usage: edf_to_tf_example "
                 " --edf_path <path_to_edf_file>"
                 " --edf_annotation_path <path_to_edf_annotation_file>"
                 " --output_path  <path_to_output>");
    return 0;
  }

  if (!args.IsSet("edf_path")) {
    ABSL_RAW_LOG(ERROR,
                 "Usage: edf_to_tf_example "
                 " --edf_path <path_to_edf_file>"
                 " --edf_annotation_path <path_to_edf_annotation_file>"
                 " --output_path  <path_to_output>");
    return 0;
  }

  string edf_path = args.GetString("edf_path").to_string();
  string annotation_path = args.GetString(
      "edf_annotation_path").to_string();

  auto segment_or = eeg_modelling::ParseEdfToSegmentProto(
      "", edf_path, "");
  eeg_modelling::Segment segment = std::move(segment_or).ValueOrDie();
  segment.set_data_type(eeg_modelling::DATATYPE_EEG);

  ParseTemplePatientInfo(segment.filename(), segment.patient_id(),
                         segment.mutable_patient_info());

  auto annotation_or = eeg_modelling::GetRawTextAnnotationForTemple(
      segment, annotation_path);
  Annotations annotations;
  *(annotations.add_annotation()) = std::move(annotation_or).ValueOrDie();

  tensorflow::Example example;
  auto example_or = GenerateExampleForSegment(segment, annotations);
  example = std::move(example_or).ValueOrDie();

  std::unique_ptr<tensorflow::WritableFile> file;
  string output_path = args.GetString("output_path").to_string();
  TF_CHECK_OK(tensorflow::Env::Default()->NewWritableFile(output_path, &file));
  tensorflow::io::RecordWriter record_writer(file.get());
  TF_CHECK_OK(record_writer.WriteRecord(example.SerializeAsString()));

  return 0;
}
