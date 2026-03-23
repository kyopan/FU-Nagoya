#!/bin/bash
# Build firmware and upload to S3
# Usage: ./build_and_upload.sh [clean]

set -e  # Exit on error

# Winch V2 S3 settings
export S3_BUCKET="comone-fragmented-unity-fw"
export S3_PREFIX="winch_v2/"
export AWS_REGION="ap-northeast-1"

echo "======================================"
echo "FU Winch V2 - Build & Upload to S3"
echo "======================================"
echo "S3 Bucket:  $S3_BUCKET"
echo "S3 Prefix:  $S3_PREFIX"
echo "AWS Region: $AWS_REGION"
echo "======================================"

# Clean build if requested
if [ "$1" == "clean" ]; then
    echo "🧹 Cleaning build directory..."
    pio run --target clean
fi

# Build and upload
echo "🔨 Building firmware..."
pio run

echo ""
echo "✅ Build and upload complete!"
echo ""
echo "📁 View files in S3:"
echo "   https://ap-northeast-1.console.aws.amazon.com/s3/buckets/comone-fragmented-unity-fw?prefix=winch_v2/"
