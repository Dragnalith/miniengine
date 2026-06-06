"""Known Android SDK license id -> SHA-1 hash table.

Transcribed verbatim from F-Droid's `sdkmanager.py`. Writing these hashes into
`<android_sdk>/licenses/<license-id>` is how Google's own tools mark a license
as accepted.
"""

KNOWN_LICENSES = {
    "android-sdk-license": "\n8933bad161af4178b1185d1a37fbf41ea5269c55\n\nd56f5187479451eabf01fb78af6dfcb131a6481e\n24333f8a63b6825ea9c5514f83c2829b004d1fee",
    "android-sdk-preview-license": "\n84831b9409646a918e30573bab4c9c91346d8abd\n",
    "android-sdk-preview-license-old": "79120722343a6f314e0719f863036c702b0e6b2a\n\n84831b9409646a918e30573bab4c9c91346d8abd",
    "intel-android-extra-license": "\nd975f751698a77b662f1254ddbeed3901e976f5a\n",
}
