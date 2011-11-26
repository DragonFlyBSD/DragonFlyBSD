/^StartFontMetrics / {
 print
 while ((getline < SYMAFM) > 0) {
  if (/^CharacterSet / || /^EncodingScheme / || /^Comment /) { print }
 }
 next
}
/^ItalicAngle 0$/ {
 print "ItalicAngle -15.5"
 next
}
/^FullName / {
 print $0 " Slanted"
 next
}
/^FSType / {
 next
}
{ print }
